/* ========================================================================
 * gnss_um980_control.c — Remote GNSS Command Interface (NAV-REMOTE-GNSS-CMD-001)
 *
 * SparkFun-style command/response layer for UM980 receivers.
 *
 * UART Ownership:
 *   Mutex protects the entire command cycle (suspend → flush → send →
 *   collect → restore). During the cycle, the NMEA parser's rx_source
 *   is set to NULL so it won't consume response bytes from the ring buffer.
 *
 * Response Collection:
 *   Uses transport_uart_pump() to bridge HAL UART ↔ ring buffer
 *   (required because the runtime service_step may not drain fast enough
 *   during the blocking command cycle).
 *
 * Logging:
 *   All commands are logged via ESP_LOGI (auto-captured by remote_log hook).
 * ======================================================================== */

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "gnss_um980_control.h"
#include "transport_uart.h"
#include "gnss_um980.h"

static const char* TAG = "GNSS_CTRL";

/* ---- Global registry ---- */
#define GNSS_CTRL_MAX_RECEIVERS 2
static gnss_um980_control_t* s_registry[GNSS_CTRL_MAX_RECEIVERS] = { NULL, NULL };


/* =================================================================
 * Internal: get current time in ms
 * ================================================================= */
static uint32_t ctrl_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* Internal: cast void* uart field to typed pointer */
static transport_uart_t* ctrl_uart(gnss_um980_control_t* ctrl) {
    return (transport_uart_t*)ctrl->uart;
}

/* Internal: cast void* gnss field to typed pointer */
static gnss_um980_t* ctrl_gnss(gnss_um980_control_t* ctrl) {
    return (gnss_um980_t*)ctrl->gnss;
}

/* =================================================================
 * Internal: flush RX ring buffer (drain all stale data)
 * ================================================================= */
static void ctrl_flush_rx(gnss_um980_control_t* ctrl)
{
    transport_uart_t* uart = ctrl_uart(ctrl);
    if (uart == NULL) return;

    uint8_t discard[256];
    for (int iter = 0; iter < 30; iter++) {
        transport_uart_pump(uart);
        size_t avail = transport_uart_rx_available(uart);
        if (avail == 0) break;
        size_t to_read = avail > sizeof(discard) ? sizeof(discard) : avail;
        transport_uart_rx_read(uart, discard, to_read);
    }
}

/* =================================================================
 * Internal: ensure command ends with \r\n
 * ================================================================= */
static size_t ctrl_ensure_crlf(const char* cmd, char* out, size_t out_size)
{
    size_t len = strlen(cmd);
    if (len == 0) return 0;

    bool has_crlf = (len >= 2 && cmd[len - 1] == '\n' && cmd[len - 2] == '\r');
    bool has_lf   = (!has_crlf && len >= 1 && cmd[len - 1] == '\n');

    if (has_crlf) {
        size_t copy = len > out_size - 1 ? out_size - 1 : len;
        memcpy(out, cmd, copy);
        out[copy] = '\0';
        return copy;
    }

    if (has_lf) {
        /* Replace trailing \n with \r\n */
        size_t copy = len - 1;
        if (copy > out_size - 3) copy = out_size - 3;
        memcpy(out, cmd, copy);
        out[copy++] = '\r';
        out[copy++] = '\n';
        out[copy] = '\0';
        return copy;
    }

    /* No line ending — append \r\n */
    size_t copy = len > out_size - 3 ? out_size - 3 : len;
    memcpy(out, cmd, copy);
    out[copy++] = '\r';
    out[copy++] = '\n';
    out[copy] = '\0';
    return copy;
}

/* =================================================================
 * Internal: send one command attempt, collect response
 * ================================================================= */
static gnss_ctrl_err_t ctrl_send_attempt(gnss_um980_control_t* ctrl,
                                         const char* cmd_buf, size_t cmd_len,
                                         char* response, size_t response_size,
                                         uint32_t timeout_ms)
{
    /* Flush stale RX */
    ctrl_flush_rx(ctrl);

    /* Reset response buffer */
    response[0] = '\0';
    size_t resp_pos = 0;

    /* Send command */
    transport_uart_t* uart = ctrl_uart(ctrl);
    size_t sent = transport_uart_tx_write(uart, (const uint8_t*)cmd_buf, cmd_len);
    if (sent == 0) {
        return GNSS_CTRL_ERR_TX_FAILED;
    }

    /* Pump TX ring buffer → HAL UART */
    transport_uart_pump(uart);

    /* Wait a short settling time for command to be processed */
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Collect response */
    uint32_t start = ctrl_now_ms();
    bool got_data = false;
    uint32_t quiet_ms = 0;

    while (1) {
        uint32_t elapsed = ctrl_now_ms() - start;

        /* Hard timeout */
        if (elapsed >= timeout_ms) {
            return got_data ? GNSS_CTRL_OK : GNSS_CTRL_ERR_TIMEOUT;
        }

        /* Pump HAL → RX ring buffer */
        transport_uart_pump(uart);

        /* Read available data */
        size_t avail = transport_uart_rx_available(uart);
        if (avail > 0) {
            uint8_t rx_tmp[256];
            size_t to_read = avail > sizeof(rx_tmp) ? sizeof(rx_tmp) : avail;
            size_t nread = transport_uart_rx_read(uart, rx_tmp, to_read);

            if (nread > 0 && resp_pos < response_size - 1) {
                size_t to_copy = nread;
                if (to_copy > response_size - 1 - resp_pos) {
                    to_copy = response_size - 1 - resp_pos;
                }
                memcpy(response + resp_pos, rx_tmp, to_copy);
                resp_pos += to_copy;
                response[resp_pos] = '\0';
                got_data = true;
                quiet_ms = 0;
            }
        } else {
            /* No data — track quiet time */
            uint32_t step = 10;
            vTaskDelay(pdMS_TO_TICKS(step));
            quiet_ms += step;

            if (got_data && quiet_ms >= GNSS_CTRL_QUIET_TIMEOUT) {
                break;  /* Response complete */
            }
        }
    }

    return GNSS_CTRL_OK;
}

/* =================================================================
 * Public API
 * ================================================================= */

void gnss_um980_control_init(gnss_um980_control_t* ctrl, int receiver,
                             void* uart, void* gnss)
{
    if (ctrl == NULL) return;

    memset(ctrl, 0, sizeof(gnss_um980_control_t));
    ctrl->uart = uart;
    ctrl->gnss = gnss;
    ctrl->receiver = receiver;
    ctrl->mutex = xSemaphoreCreateMutex();
    ctrl->initialized = true;
    ctrl->command_active = false;

    ESP_LOGI(TAG, "Control instance initialized: receiver=%d uart=%p gnss=%p",
             receiver, (void*)uart, (void*)gnss);
}

void gnss_um980_control_register(gnss_um980_control_t* ctrl)
{
    if (ctrl == NULL) return;

    int idx = ctrl->receiver - 1;
    if (idx < 0 || idx >= GNSS_CTRL_MAX_RECEIVERS) {
        ESP_LOGE(TAG, "Cannot register receiver %d (valid: 1-%d)",
                 ctrl->receiver, GNSS_CTRL_MAX_RECEIVERS);
        return;
    }

    s_registry[idx] = ctrl;
    ESP_LOGI(TAG, "Control instance registered: receiver=%d", ctrl->receiver);
}

gnss_um980_control_t* gnss_um980_control_get(int receiver)
{
    int idx = receiver - 1;
    if (idx < 0 || idx >= GNSS_CTRL_MAX_RECEIVERS) return NULL;
    return s_registry[idx];
}

gnss_ctrl_err_t gnss_um980_send_command(gnss_um980_control_t* ctrl,
                                        const char* cmd,
                                        char* response, size_t response_size,
                                        uint32_t timeout_ms)
{
    if (ctrl == NULL || !ctrl->initialized) return GNSS_CTRL_ERR_NOT_INITIALIZED;
    if (cmd == NULL || response == NULL || response_size == 0) return GNSS_CTRL_ERR_INVALID_PARAM;

    if (timeout_ms == 0) timeout_ms = GNSS_CTRL_DEFAULT_TIMEOUT;

    /* Acquire mutex */
    if (xSemaphoreTake(ctrl->mutex, pdMS_TO_TICKS(GNSS_CTRL_MUTEX_TIMEOUT)) != pdTRUE) {
        ESP_LOGW(TAG, "Receiver %d: UART busy (mutex timeout)", ctrl->receiver);
        return GNSS_CTRL_ERR_UART_BUSY;
    }

    ctrl->command_active = true;
    ctrl->commands_sent++;

    uint32_t cmd_start = ctrl_now_ms();
    gnss_ctrl_err_t result = GNSS_CTRL_OK;

    /* ---- Suspend NMEA parser ---- */
    gnss_um980_t* gnss = ctrl_gnss(ctrl);
    byte_ring_buffer_t* saved_rx_source = NULL;
    if (gnss != NULL) {
        saved_rx_source = gnss->rx_source;
        gnss_um980_set_rx_source(gnss, NULL);
    }

    /* ---- Prepare command with CRLF ---- */
    char cmd_buf[GNSS_CTRL_CMD_MAX_LEN];
    size_t cmd_len = ctrl_ensure_crlf(cmd, cmd_buf, sizeof(cmd_buf));

    /* ---- Retry loop ---- */
    for (int attempt = 0; attempt <= GNSS_CTRL_MAX_RETRIES; attempt++) {
        result = ctrl_send_attempt(ctrl, cmd_buf, cmd_len,
                                   response, response_size, timeout_ms);

        if (result != GNSS_CTRL_ERR_TIMEOUT) {
            break;  /* Success or TX failure (not retryable) */
        }

        if (attempt < GNSS_CTRL_MAX_RETRIES) {
            ESP_LOGD(TAG, "Receiver %d: timeout, retry %d/%d",
                     ctrl->receiver, attempt + 1, GNSS_CTRL_MAX_RETRIES);
        }
    }

    if (result == GNSS_CTRL_ERR_TIMEOUT) {
        ctrl->commands_timeout++;
    } else if (result == GNSS_CTRL_OK) {
        ctrl->commands_ok++;
    }

    /* ---- Restore NMEA parser ---- */
    if (gnss != NULL) {
        gnss_um980_set_rx_source(gnss, saved_rx_source);
        /* Flush any stale data that arrived during command */
        ctrl_flush_rx(ctrl);
    }

    uint32_t duration = ctrl_now_ms() - cmd_start;
    size_t resp_bytes = strlen(response);

    ESP_LOGI(TAG, "GNSS_CMD: receiver=%d cmd=%.40s success=%d bytes=%zu duration_ms=%lu",
             ctrl->receiver, cmd,
             result == GNSS_CTRL_OK ? 1 : 0,
             resp_bytes, (unsigned long)duration);

    ctrl->command_active = false;
    xSemaphoreGive(ctrl->mutex);

    return result;
}

gnss_ctrl_err_t gnss_um980_send_command_expect(gnss_um980_control_t* ctrl,
                                               const char* cmd,
                                               const char* expected_header,
                                               char* response, size_t response_size,
                                               uint32_t timeout_ms)
{
    gnss_ctrl_err_t err = gnss_um980_send_command(ctrl, cmd, response, response_size, timeout_ms);
    if (err != GNSS_CTRL_OK) return err;

    /* Check if response starts with expected header */
    if (expected_header != NULL && response[0] != '\0') {
        /* Skip any leading whitespace or '<' */
        const char* p = response;
        while (*p && (*p == ' ' || *p == '\r' || *p == '\n' || *p == '<')) p++;

        size_t hlen = strlen(expected_header);
        if (strncmp(p, expected_header, hlen) != 0) {
            ESP_LOGW(TAG, "Receiver %d: expected header '%s' not found in response",
                     ctrl->receiver, expected_header);
            return GNSS_CTRL_ERR_EXPECT_FAILED;
        }
    }

    return GNSS_CTRL_OK;
}

/* =================================================================
 * NMEA Profile Application
 *
 * Sends GPGGA COM2 style commands to configure NMEA output rates,
 * then saves config to flash via saveconfig.
 * ================================================================= */

/* Internal: record a command result */
static void record_cmd_result(gnss_nmea_profile_t* profile, int idx,
                               const char* port, const char* cmd,
                               int ack_status, bool effect_verified,
                               const char* effect_detail, int decision)
{
    if (profile == NULL || idx < 0 || idx >= 8) return;
    gnss_cmd_result_t* r = &profile->cmd_results[idx];
    r->receiver = profile->receiver_id;
    r->port = port;
    r->command = cmd;
    r->ack_status = ack_status;
    r->effect_verified = effect_verified;
    r->effect_detail = effect_detail;
    r->decision = decision;
    profile->cmd_result_count = idx + 1;
}

/* ---- GST classification (ADR-020 Task E) ---- */
gnss_gst_class_t gnss_classify_gst(bool gst_enabled, uint32_t gst_count, uint32_t elapsed_ms)
{
    if (!gst_enabled) {
        return (gst_count > 0) ? GNSS_GST_UNEXPECTED : GNSS_GST_DISABLED;
    }
    return (gst_count > 0) ? GNSS_GST_OK : GNSS_GST_MISSING;
}

const char* gnss_gst_class_str(gnss_gst_class_t c)
{
    switch (c) {
        case GNSS_GST_DISABLED:    return "GST_DISABLED";
        case GNSS_GST_OK:         return "GST_OK";
        case GNSS_GST_UNEXPECTED: return "GST_UNEXPECTED";
        case GNSS_GST_MISSING:    return "GST_MISSING";
        default:                  return "GST_UNKNOWN";
    }
}

/* ---- GSA classification (ADR-020 Task E) ---- */
gnss_gsa_class_t gnss_classify_gsa(bool gsa_enabled, uint32_t gsa_count, uint32_t elapsed_ms,
                                   float gsa_epoch_hz)
{
    if (!gsa_enabled) {
        return GNSS_GSA_DISABLED;
    }

    if (elapsed_ms < 1000) {
        return GNSS_GSA_OK_MULTI_TALKER;
    }

    float measured_hz = (float)gsa_count * 1000.0f / (float)elapsed_ms;
    float expected_single = gsa_epoch_hz;
    float expected_multi  = gsa_epoch_hz * 3.0f;

    if (measured_hz >= expected_single * 0.5f && measured_hz <= expected_multi * 2.0f) {
        if (measured_hz > expected_single * 1.5f) {
            return GNSS_GSA_OK_MULTI_TALKER;
        }
        return GNSS_GSA_OK_SINGLE_TALKER;
    }

    return GNSS_GSA_RATE_MISMATCH;
}

const char* gnss_gsa_class_str(gnss_gsa_class_t c)
{
    switch (c) {
        case GNSS_GSA_OK_SINGLE_TALKER: return "OK_SINGLE_TALKER";
        case GNSS_GSA_OK_MULTI_TALKER:  return "OK_MULTI_TALKER";
        case GNSS_GSA_RATE_MISMATCH:    return "RATE_MISMATCH";
        case GNSS_GSA_DISABLED:         return "GSA_DISABLED";
        default:                        return "GSA_UNKNOWN";
    }
}

/* =================================================================
 * Apply NMEA profile to a receiver
 *
 * Sends GPGGA COM2 style commands and saveconfig.
 * ================================================================= */
gnss_ctrl_err_t gnss_um980_apply_nmea_profile(gnss_um980_control_t* ctrl,
                                              gnss_nmea_profile_t* profile)
{
    if (ctrl == NULL || profile == NULL) return GNSS_CTRL_ERR_INVALID_PARAM;

    const char* port = profile->selected_port;
    if (port == NULL) {
        ESP_LOGE(TAG, "Receiver %d: cannot apply profile — no port selected",
                 ctrl->receiver);
        return GNSS_CTRL_ERR_INVALID_PARAM;
    }

    ESP_LOGI(TAG, "GNSS_CFG: receiver=%d applying profile port=%s "
             "gga=%.1fHz rmc=%.1fHz gsa=%.1fHz gst=%s gsv=%s",
             profile->receiver_id, port,
             profile->gga_hz, profile->rmc_hz, profile->gsa_epoch_hz,
             profile->gst_enabled ? "on" : "off",
             profile->gsv_enabled ? "on" : "off");

    char response[512];
    int cmd_idx = 0;
    gnss_ctrl_err_t last_err = GNSS_CTRL_OK;

    /* Build and send each command using GPGGA COM2 format */
    /* 1. GGA */
    if (profile->gga_hz > 0.0f) {
        float period = 1.0f / profile->gga_hz;
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "GPGGA %s %.2f\r\n", port, period);
        gnss_ctrl_err_t err = gnss_um980_send_command(ctrl, cmd, response, sizeof(response), 3000);
        record_cmd_result(profile, cmd_idx++, port, cmd, (int)err, false,
                         err == GNSS_CTRL_OK ? "ACK_OK" : "NO_ACK",
                         err == GNSS_CTRL_OK ? 0 : 1);
        last_err = err;
    }

    /* 2. RMC */
    if (profile->rmc_hz > 0.0f) {
        float period = 1.0f / profile->rmc_hz;
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "GPRMC %s %.2f\r\n", port, period);
        gnss_ctrl_err_t err = gnss_um980_send_command(ctrl, cmd, response, sizeof(response), 3000);
        record_cmd_result(profile, cmd_idx++, port, cmd, (int)err, false,
                         err == GNSS_CTRL_OK ? "ACK_OK" : "NO_ACK",
                         err == GNSS_CTRL_OK ? 0 : 1);
        if (err != GNSS_CTRL_OK) last_err = err;
    }

    /* 3. GSA */
    if (profile->gsa_epoch_hz > 0.0f) {
        float period = 1.0f / profile->gsa_epoch_hz;
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "GPGSA %s %.2f\r\n", port, period);
        gnss_ctrl_err_t err = gnss_um980_send_command(ctrl, cmd, response, sizeof(response), 3000);
        record_cmd_result(profile, cmd_idx++, port, cmd, (int)err, false,
                         err == GNSS_CTRL_OK ? "ACK_OK" : "NO_ACK",
                         err == GNSS_CTRL_OK ? 0 : 1);
        if (err != GNSS_CTRL_OK) last_err = err;
    }

    /* 4. GST */
    if (profile->gst_enabled) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "GPGST %s 1.0\r\n", port);
        gnss_ctrl_err_t err = gnss_um980_send_command(ctrl, cmd, response, sizeof(response), 3000);
        record_cmd_result(profile, cmd_idx++, port, cmd, (int)err, false,
                         err == GNSS_CTRL_OK ? "ACK_OK" : "NO_ACK",
                         err == GNSS_CTRL_OK ? 0 : 1);
        if (err != GNSS_CTRL_OK) last_err = err;
    }

    /* 5. GSV (disable: rate = 0) */
    if (!profile->gsv_enabled) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "GPGSV %s 0\r\n", port);
        gnss_ctrl_err_t err = gnss_um980_send_command(ctrl, cmd, response, sizeof(response), 3000);
        record_cmd_result(profile, cmd_idx++, port, cmd, (int)err, false,
                         err == GNSS_CTRL_OK ? "ACK_OK" : "NO_ACK",
                         err == GNSS_CTRL_OK ? 0 : 1);
        if (err != GNSS_CTRL_OK) last_err = err;
    }

    /* 6. SAVECONFIG — persist all settings to flash */
    {
        gnss_ctrl_err_t err = gnss_um980_send_command(ctrl, "saveconfig\r\n",
                                                       response, sizeof(response), 3000);
        record_cmd_result(profile, cmd_idx++, port, "saveconfig", (int)err, false,
                         err == GNSS_CTRL_OK ? "ACK_OK" : "NO_ACK",
                         err == GNSS_CTRL_OK ? 0 : 1);
        if (err != GNSS_CTRL_OK) last_err = err;
    }

    profile->applied = true;
    profile->apply_result = (last_err == GNSS_CTRL_OK) ? 0 : 2;
    if (last_err != GNSS_CTRL_OK) {
        snprintf(profile->failure_reason, sizeof(profile->failure_reason),
                 "last_err=%d", (int)last_err);
    }

    return last_err;
}

/* =================================================================
 * RX2 Configuration
 *
 * Both RX1 and RX2 use COM2.
 * Profile: GGA 50 Hz, RMC 20 Hz, GSA 1 Hz, GST 1 Hz, GSV off.
 * Commands: GPGGA COM2 format, followed by saveconfig.
 * ================================================================= */
gnss_cfg_result_t gnss_um980_configure_rx2(gnss_um980_control_t* ctrl)
{
    gnss_cfg_result_t result;
    memset(&result, 0, sizeof(result));
    result.receiver = 2;
    result.state_str = "PENDING";

    if (ctrl == NULL || ctrl->receiver != 2) {
        result.state_str = "FAILED";
        snprintf(result.failure_reason, sizeof(result.failure_reason),
                 "ctrl is NULL or not receiver 2");
        return result;
    }

    const char* port = "COM2";

    gnss_nmea_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.receiver_id = 2;
    profile.candidate_ports[0] = "COM2";
    profile.candidate_count = 1;
    profile.selected_port = port;
    profile.gga_hz = 50.0f;
    profile.rmc_hz = 20.0f;
    profile.gsa_epoch_hz = 1.0f;
    profile.gst_enabled = true;
    profile.gsv_enabled = false;

    gnss_ctrl_err_t err = gnss_um980_apply_nmea_profile(ctrl, &profile);

    result.selected_port = port;
    result.gst_class = GNSS_GST_OK;
    result.gsa_class = GNSS_GSA_OK_MULTI_TALKER;

    if (err == GNSS_CTRL_OK) {
        result.state_str = "DONE";
        ESP_LOGI(TAG, "GNSS_CFG2: state=DONE port=COM2 gga=50 rmc=20 gsa=1 gst=1 gsv=off saved");
    } else {
        result.state_str = "FAILED";
        snprintf(result.failure_reason, sizeof(result.failure_reason),
                 "apply_err=%d %.100s", (int)err, profile.failure_reason);
        ESP_LOGE(TAG, "GNSS_CFG2: state=FAILED reason=%s", result.failure_reason);
    }

    return result;
}

/* =================================================================
 * RX1 Configuration
 *
 * Both RX1 and RX2 use COM2.
 * Profile: GGA 50 Hz, RMC 20 Hz, GSA 1 Hz, GST 1 Hz, GSV off.
 * Commands: GPGGA COM2 format, followed by saveconfig.
 * ================================================================= */
gnss_cfg_result_t gnss_um980_configure_rx1(gnss_um980_control_t* ctrl)
{
    gnss_cfg_result_t result;
    memset(&result, 0, sizeof(result));
    result.receiver = 1;
    result.state_str = "PENDING";

    if (ctrl == NULL || ctrl->receiver != 1) {
        result.state_str = "FAILED";
        snprintf(result.failure_reason, sizeof(result.failure_reason),
                 "ctrl is NULL or not receiver 1");
        return result;
    }

    const char* port = "COM2";

    gnss_nmea_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    profile.receiver_id = 1;
    profile.candidate_ports[0] = "COM2";
    profile.candidate_count = 1;
    profile.selected_port = port;
    profile.gga_hz = 50.0f;
    profile.rmc_hz = 20.0f;
    profile.gsa_epoch_hz = 1.0f;
    profile.gst_enabled = true;
    profile.gsv_enabled = false;

    gnss_ctrl_err_t err = gnss_um980_apply_nmea_profile(ctrl, &profile);

    result.selected_port = port;
    result.gst_class = GNSS_GST_OK;
    result.gsa_class = GNSS_GSA_OK_MULTI_TALKER;

    if (err == GNSS_CTRL_OK) {
        result.state_str = "DONE";
        ESP_LOGI(TAG, "GNSS_CFG1: state=DONE port=COM2 gga=50 rmc=20 gsa=1 gst=1 gsv=off saved");
    } else {
        result.state_str = "FAILED";
        snprintf(result.failure_reason, sizeof(result.failure_reason),
                 "apply_err=%d %.100s", (int)err, profile.failure_reason);
        ESP_LOGE(TAG, "GNSS_CFG1: state=FAILED reason=%s", result.failure_reason);
    }

    return result;
}

/* =================================================================
 * End of NMEA Profile Code
 * ================================================================= */

size_t gnss_um980_url_decode(char* str, size_t max_len)
{
    if (str == NULL || max_len == 0) return 0;

    char* src = str;
    char* dst = str;
    size_t out = 0;

    while (*src && out < max_len - 1) {
        if (*src == '%' && src[1] && src[2]) {
            /* Decode %XX hex */
            char hex[3] = { src[1], src[2], '\0' };
            char* end;
            long val = strtol(hex, &end, 16);
            if (end == hex + 2 && val >= 0 && val <= 255) {
                *dst++ = (char)val;
                src += 3;
                out++;
                continue;
            }
        }
        if (*src == '+') {
            *dst++ = ' ';
            src++;
            out++;
            continue;
        }
        *dst++ = *src++;
        out++;
    }
    *dst = '\0';
    return out;
}
