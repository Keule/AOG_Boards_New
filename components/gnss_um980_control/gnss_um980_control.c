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

/* ---- Command blocklist (case-insensitive prefix match) ---- */
static const char* const s_blocked_prefixes[] = {
    "FRESET",
    "SAVECONFIG",
    "RESET",
    "UPGRADE",
};
#define BLOCKED_PREFIX_COUNT (sizeof(s_blocked_prefixes) / sizeof(s_blocked_prefixes[0]))

/* ---- NMEA restore commands (sent after UNLOGALL at boot) ---- */
static const char* const s_nmea_restore_cmds[] = {
    "LOG COM2 GNGGA ONTIME 0.05\r\n",
    "LOG COM2 GNRMC ONTIME 0.05\r\n",
    "LOG COM2 GNGST ONTIME 0.05\r\n",
    "LOG COM2 GNGSA ONTIME 1\r\n",
};
#define NMEA_RESTORE_CMD_COUNT (sizeof(s_nmea_restore_cmds) / sizeof(s_nmea_restore_cmds[0]))

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

bool gnss_um980_control_is_command_blocked(const char* cmd)
{
    if (cmd == NULL || cmd[0] == '\0') return true;

    /* Extract first word (uppercase) for prefix match */
    char word[64];
    int i;
    for (i = 0; i < 63 && cmd[i]; i++) {
        if (cmd[i] == ' ' || cmd[i] == '\r' || cmd[i] == '\n' || cmd[i] == '\t') {
            break;
        }
        word[i] = (char)toupper((unsigned char)cmd[i]);
    }
    word[i] = '\0';

    if (i == 0) return true;  /* Empty command */

    for (size_t b = 0; b < BLOCKED_PREFIX_COUNT; b++) {
        size_t plen = strlen(s_blocked_prefixes[b]);
        if ((size_t)i >= plen && memcmp(word, s_blocked_prefixes[b], plen) == 0) {
            return true;
        }
    }
    return false;
}

gnss_ctrl_err_t gnss_um980_send_command(gnss_um980_control_t* ctrl,
                                        const char* cmd,
                                        char* response, size_t response_size,
                                        uint32_t timeout_ms)
{
    if (ctrl == NULL || !ctrl->initialized) return GNSS_CTRL_ERR_NOT_INITIALIZED;
    if (cmd == NULL || response == NULL || response_size == 0) return GNSS_CTRL_ERR_INVALID_PARAM;

    /* Check blocklist */
    if (gnss_um980_control_is_command_blocked(cmd)) {
        ctrl->commands_blocked++;
        ESP_LOGW(TAG, "Receiver %d: BLOCKED command: %.40s", ctrl->receiver, cmd);
        return GNSS_CTRL_ERR_BLOCKED;
    }

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
 * Boot-time helpers (used by snapshot refactor)
 * ================================================================= */

/**
 * Send UNLOGALL to stop all NMEA logging.
 * Typically called before querying config to get clean responses.
 */
gnss_ctrl_err_t gnss_um980_control_unlogall(gnss_um980_control_t* ctrl)
{
    char response[256];
    return gnss_um980_send_command(ctrl, "UNLOGALL", response, sizeof(response), 2000);
}

/**
 * Restore essential NMEA logs after UNLOGALL.
 * Sends LOG commands for GGA, RMC, GST on COM1.
 */
void gnss_um980_control_restore_nmea(gnss_um980_control_t* ctrl)
{
    char response[256];
    for (size_t i = 0; i < NMEA_RESTORE_CMD_COUNT; i++) {
        /* Strip \r\n for logging */
        char cmd_display[64];
        const char* src = s_nmea_restore_cmds[i];
        size_t len = strlen(src);
        if (len > 63) len = 63;
        memcpy(cmd_display, src, len);
        cmd_display[len] = '\0';
        /* Remove trailing \r\n */
        char* nl = strchr(cmd_display, '\r');
        if (nl) *nl = '\0';

        gnss_ctrl_err_t err = gnss_um980_send_command(ctrl, s_nmea_restore_cmds[i],
                                                       response, sizeof(response), 2000);
        if (err != GNSS_CTRL_OK) {
            ESP_LOGW(TAG, "Receiver %d: NMEA restore '%s' failed (err=%d)",
                     ctrl->receiver, cmd_display, (int)err);
        } else {
            ESP_LOGI(TAG, "Receiver %d: NMEA restore '%s' OK", ctrl->receiver, cmd_display);
        }
    }
}

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
