/* ========================================================================
 * gnss_um980_control.c — Remote GNSS Command Interface (NAV-REMOTE-GNSS-CMD-001)
 *                       + NAV-GNSS-NMEA-LOG-IDEMPOTENT-001
 *
 * SparkFun-style command/response layer for UM980 receivers.
 *
 * UART Ownership:
 *   Mutex protects the entire command cycle (suspend → flush → send →
 *   collect → restore). During the cycle, the NMEA parser's rx_source
 *   is set to NULL so it won't consume response bytes from the ring buffer.
 *
 * NAV-GNSS-NMEA-LOG-IDEMPOTENT-001 changes:
 *   - Idempotent restore: targeted UNLOG before LOG (all talker IDs)
 *   - Explicit GSV deactivation (GNGSV, GPGSV, GBGSV, GAGSV)
 *   - Only LOG COM2 <sentence> ONTIME <period> syntax (no short-form)
 *   - Restore command result verification (response OK check)
 *   - Profile state tracking (IDLE/RESTORING/ACTIVE/HIGH_RATE/...)
 *   - Rate guard: monitors NMEA rates after restore, triggers auto-recovery
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
    "UPGRADE",
};
#define BLOCKED_PREFIX_COUNT (sizeof(s_blocked_prefixes) / sizeof(s_blocked_prefixes[0]))

/* ---- UNLOG commands (Phase 1: deactivate all logs on COM2) ----
 * Covers all known talker IDs for each sentence type.
 * UM980 supports GN, GP, GA, GB talker IDs. */
static const char* const s_unlog_cmds[] = {
    "UNLOG COM2 GNGGA\r\n",
    "UNLOG COM2 GPGGA\r\n",
    "UNLOG COM2 GNRMC\r\n",
    "UNLOG COM2 GPRMC\r\n",
    "UNLOG COM2 GNGST\r\n",
    "UNLOG COM2 GPGST\r\n",
    "UNLOG COM2 GNGSA\r\n",
    "UNLOG COM2 GPGSA\r\n",
    "UNLOG COM2 GNGSV\r\n",    /* Explicit GSV deactivation */
    "UNLOG COM2 GPGSV\r\n",
    "UNLOG COM2 GBGSV\r\n",
    "UNLOG COM2 GAGSV\r\n",
};
#define UNLOG_CMD_COUNT (sizeof(s_unlog_cmds) / sizeof(s_unlog_cmds[0]))

/* ---- NMEA restore commands (Phase 2: activate runtime profile) ----
 * Uses ONLY LOG COM2 <SENTENCE> ONTIME <PERIOD> syntax.
 * No short-form commands (e.g. "GNGGA COM2 0.05").
 *
 * GGA/RMC/GST: 20 Hz (0.05s) — core navigation sentences
 * GSA: 1 Hz — DOP info for quality assessment
 * GSV: DISABLED — removed to reduce ~40-60% of NMEA traffic volume */
static const char* const s_nmea_restore_cmds[] = {
    "LOG COM2 GNGGA ONTIME 0.05\r\n",
    "LOG COM2 GNRMC ONTIME 0.05\r\n",
    "LOG COM2 GNGST ONTIME 0.05\r\n",
    "LOG COM2 GNGSA ONTIME 1\r\n",
    /* GSV intentionally omitted — disabled in Phase 1 */
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
 * Internal: check if response contains OK
 * ================================================================= */
static bool response_contains_ok(const char* response)
{
    if (response == NULL || response[0] == '\0') return false;
    /* UM980 responses: "<OK>", "OK", "OK\r\n", "<OK>\r\n" */
    const char* p = response;
    while (*p) {
        if ((*p == 'O' || *p == 'o') && *(p + 1) == 'K') return true;
        p++;
    }
    return false;
}

/* =================================================================
 * Internal: strip \r\n for logging
 * ================================================================= */
static void strip_crlf(char* str)
{
    if (str == NULL) return;
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[--len] = '\0';
    }
}

/* =================================================================
 * Internal: store restore result
 * ================================================================= */
static void store_restore_result(gnss_um980_control_t* ctrl,
                                  const char* cmd, gnss_ctrl_err_t err,
                                  const char* response)
{
    if (ctrl->restore_result_count >= GNSS_CTRL_RESTORE_RESULTS_MAX) return;
    gnss_ctrl_restore_result_t* r = &ctrl->restore_results[ctrl->restore_result_count++];
    r->cmd = cmd;
    r->err = err;
    r->response_len = response ? (uint32_t)strlen(response) : 0;
    r->response_has_ok = response_contains_ok(response);
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
    ctrl->profile_state = NMEA_PROFILE_IDLE;

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
 * Boot-time helpers
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

/* =================================================================
 * NAV-GNSS-NMEA-LOG-IDEMPOTENT-001: Idempotent NMEA profile restore
 * ================================================================= */

gnss_ctrl_err_t gnss_um980_control_restore_nmea(gnss_um980_control_t* ctrl)
{
    if (ctrl == NULL || !ctrl->initialized) return GNSS_CTRL_ERR_NOT_INITIALIZED;

    char response[256];
    char cmd_display[64];
    bool any_failed = false;

    ctrl->profile_state = NMEA_PROFILE_RESTORING;
    ctrl->restore_result_count = 0;
    ctrl->profile_restore_count++;
    ctrl->profile_last_restore_ms = ctrl_now_ms();

    ESP_LOGI(TAG, "Receiver %d: === Idempotent NMEA restore start ===", ctrl->receiver);

    /* ---- Phase 1: Deactivate all logs on COM2 ---- */
    ESP_LOGI(TAG, "Receiver %d: Phase 1 — Deactivating all COM2 logs...", ctrl->receiver);

    for (size_t i = 0; i < UNLOG_CMD_COUNT; i++) {
        const char* cmd = s_unlog_cmds[i];

        /* Copy for logging (strip CRLF) */
        strncpy(cmd_display, cmd, sizeof(cmd_display) - 1);
        cmd_display[sizeof(cmd_display) - 1] = '\0';
        strip_crlf(cmd_display);

        gnss_ctrl_err_t err = gnss_um980_send_command(ctrl, cmd,
                                                       response, sizeof(response), 100);
        store_restore_result(ctrl, cmd, err, response);

        if (err != GNSS_CTRL_OK) {
            /* UNLOG failure is a warning, not a hard error —
             * the sentence may not have been active. Continue. */
            ESP_LOGW(TAG, "Receiver %d: UNLOG '%s' warning (err=%d)",
                     ctrl->receiver, cmd_display, (int)err);
        }
    }

    /* Small delay between phases for UM980 to process */
    vTaskDelay(pdMS_TO_TICKS(200));

    /* ---- Phase 2: Activate runtime profile ---- */
    ESP_LOGI(TAG, "Receiver %d: Phase 2 — Activating runtime profile...", ctrl->receiver);

    for (size_t i = 0; i < NMEA_RESTORE_CMD_COUNT; i++) {
        const char* cmd = s_nmea_restore_cmds[i];

        strncpy(cmd_display, cmd, sizeof(cmd_display) - 1);
        cmd_display[sizeof(cmd_display) - 1] = '\0';
        strip_crlf(cmd_display);

        gnss_ctrl_err_t err = gnss_um980_send_command(ctrl, cmd,
                                                       response, sizeof(response), 2000);
        store_restore_result(ctrl, cmd, err, response);

        if (err != GNSS_CTRL_OK) {
            ESP_LOGW(TAG, "Receiver %d: RESTORE '%s' FAILED (err=%d)",
                     ctrl->receiver, cmd_display, (int)err);
            any_failed = true;
            ctrl->profile_last_error = err;
        } else if (!response_contains_ok(response)) {
            /* Command sent but no OK in response — suspicious */
            ESP_LOGW(TAG, "Receiver %d: RESTORE '%s' sent but no OK in response",
                     ctrl->receiver, cmd_display);
            any_failed = true;
            ctrl->profile_last_error = GNSS_CTRL_ERR_EXPECT_FAILED;
        } else {
            ESP_LOGI(TAG, "Receiver %d: RESTORE '%s' OK", ctrl->receiver, cmd_display);
        }
    }

    /* Start rate guard window */
    ctrl->profile_rate_guard_active = true;
    ctrl->profile_rate_guard_start_ms = ctrl_now_ms();

    if (any_failed) {
        ctrl->profile_state = NMEA_PROFILE_RECOVERY_FAILED;
        ESP_LOGE(TAG, "Receiver %d: === NMEA restore FAILED ===", ctrl->receiver);
        return GNSS_CTRL_ERR_RESTORE_FAILED;
    }

    ctrl->profile_state = NMEA_PROFILE_ACTIVE;
    ctrl->profile_last_error = GNSS_CTRL_OK;
    ESP_LOGI(TAG, "Receiver %d: === NMEA restore OK ===", ctrl->receiver);
    return GNSS_CTRL_OK;
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
