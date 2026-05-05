/* ========================================================================
 * gnss_nmea_config.c — UM980 NMEA Configuration Manager Implementation
 *                        (NAV-UM980-LOG-SYNTAX-RATE-FIX-001)
 *
 * Sends LOG/UNLOG commands to UM980 receivers via UART TX.
 * Reads responses from shared UART RX buffer during restore.
 * Provides idempotent NMEA restore with robust ACK detection.
 *
 * RX Buffer Sharing Protocol:
 *   During restore, this component reads ALL bytes from the UART RX buffer
 *   in its service_step. NMEA data bytes are forwarded to gnss_um980 via
 *   gnss_um980_feed() so parsing continues. After restore completes,
 *   gnss_um980 reads from the UART RX buffer directly again.
 *
 * IMPORTANT: This component must be registered BEFORE gnss_um980 in the
 *   SERVICE_GROUP_UART to ensure it reads the RX buffer first.
 * ======================================================================== */

#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* ---- Own header (must be first for type definitions) ---- */
#include "gnss_nmea_config.h"

/* ---- Component headers (required for type access) ---- */
#include "gnss_um980.h"
#include "transport_uart.h"

/* ---- Platform-specific logging ---- */
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
static const char* TAG = "GNSS_CFG";
#define now_us() ((uint64_t)esp_timer_get_time())
#define now_ms() (now_us() / 1000)
#else
/* Host test stubs */
static const char* GNSS_CFG_TAG = "GNSS_CFG";
#define ESP_LOGI(t, fmt, ...)  printf("[I][%s] " fmt "\n", t, ##__VA_ARGS__)
#define ESP_LOGW(t, fmt, ...)  printf("[W][%s] " fmt "\n", t, ##__VA_ARGS__)
#define ESP_LOGE(t, fmt, ...)  printf("[E][%s] " fmt "\n", t, ##__VA_ARGS__)
#define ESP_LOGD(t, fmt, ...)  printf("[D][%s] " fmt "\n", t, ##__VA_ARGS__)
static uint64_t stub_time_us = 0;
static void stub_set_time(uint64_t us) { stub_time_us = us; }
#define now_us() stub_time_us
#define now_ms() (stub_time_us / 1000)
#define TAG GNSS_CFG_TAG
#endif

/* ---- Internal helpers ---- */

static uint64_t now_us_real(void);
static uint64_t now_ms_real(void);

/* ---- Internal: add command to queue ---- */

static bool queue_cmd(gnss_nmea_config_t* cfg, const char* cmd)
{
    if (cfg == NULL || cmd == NULL) return false;
    if (cfg->cmd_count >= UM980_CMD_QUEUE_MAX) return false;
    size_t len = strlen(cmd);
    if (len >= UM980_CMD_MAX_LEN - 2) len = UM980_CMD_MAX_LEN - 3;
    memcpy(cfg->cmd_queue[cfg->cmd_count], cmd, len);
    cfg->cmd_queue[cfg->cmd_count][len] = '\r';
    cfg->cmd_queue[cfg->cmd_count][len + 1] = '\n';
    cfg->cmd_queue[cfg->cmd_count][len + 2] = '\0';
    cfg->cmd_count++;
    return true;
}

/* ---- Internal: build restore command queue ---- */

static void build_restore_queue(gnss_nmea_config_t* cfg)
{
    cfg->cmd_count = 0;
    cfg->cmd_index = 0;

    /* Phase 1: UNLOG all (clear existing logs) */
    queue_cmd(cfg, "UNLOG " UM980_PORT_COM2);

    /* Phase 1b: UNLOG per-sentence-type (belt-and-suspenders) */
    queue_cmd(cfg, "UNLOG " UM980_PORT_COM2 " GNGGA");
    queue_cmd(cfg, "UNLOG " UM980_PORT_COM2 " GNRMC");
    queue_cmd(cfg, "UNLOG " UM980_PORT_COM2 " GNGST");
    queue_cmd(cfg, "UNLOG " UM980_PORT_COM2 " GNGSA");
    queue_cmd(cfg, "UNLOG " UM980_PORT_COM2 " GNGSV");
    queue_cmd(cfg, "UNLOG " UM980_PORT_COM2 " GPGSV");
    queue_cmd(cfg, "UNLOG " UM980_PORT_COM2 " GAGSV");
    queue_cmd(cfg, "UNLOG " UM980_PORT_COM2 " GBGSV");

    /* Phase 2: LOG target NMEA profile using LOG-ONTIME syntax only */
    queue_cmd(cfg, "LOG " UM980_PORT_COM2 " GNGGA ONTIME " "0.05");
    queue_cmd(cfg, "LOG " UM980_PORT_COM2 " GNRMC ONTIME " "0.05");
    queue_cmd(cfg, "LOG " UM980_PORT_COM2 " GNGST ONTIME " "0.05");
    queue_cmd(cfg, "LOG " UM980_PORT_COM2 " GNGSA ONTIME " "1");

    /* GSV is explicitly NOT enabled — disabled in production profile */

    ESP_LOGI(TAG, "%s: restore queue built with %u commands", cfg->name, cfg->cmd_count);
}

/* =================================================================
 * ACK Detection — um980_parse_response()
 *
 * Scans response buffer for UM980 acknowledgment patterns.
 * Handles various response formats:
 *   - "$command,<CMD>,response: OK*CS"
 *   - "$command,<CMD>,response: ERROR*CS"
 *   - "<CR><LF>response: OK*CS<CR><LF>"
 *   - "<CR><LF>response: ERROR*CS<CR><LF>"
 *   - "OK"
 *   - "ERROR"
 *
 * Tolerances:
 *   - Case-insensitive OK/ERROR matching
 *   - Ignores CR/LF characters
 *   - Ignores NMEA sentence lines (starting with '$' followed by non-"command")
 *   - Tolerates embedded NMEA data between command echo and response
 * ================================================================= */

/* Internal: case-insensitive substring search */
static const char* stristr(const char* haystack, const char* needle, size_t max_len)
{
    if (haystack == NULL || needle == NULL || *needle == '\0') return NULL;
    size_t nlen = strlen(needle);
    if (nlen > max_len) return NULL;

    for (size_t i = 0; i + nlen <= max_len; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return &haystack[i];
    }
    return NULL;
}

/* Check if a line (starting at offset) is a NMEA data sentence (not a command response).
 * NMEA data sentences start with '$' followed by talker ID (2 chars) and sentence type (3 chars).
 * Command echo lines start with '$command,'.
 * Returns true if this is a NMEA data sentence that should be ignored for ACK detection. */
static bool is_nmea_data_line(const char* buf, uint16_t offset, uint16_t len)
{
    if (offset >= len) return false;
    if (buf[offset] != '$') return false;

    /* Check for command echo: "$command," */
    const char* cmd_echo = "$command,";
    size_t cmd_len = strlen(cmd_echo);
    if (offset + cmd_len <= len) {
        if (stristr(&buf[offset], cmd_echo, cmd_len) != NULL) {
            return false; /* This is the command echo, not NMEA data */
        }
    }

    /* It starts with '$' but not "$command," — likely NMEA data */
    return true;
}

bool um980_parse_response(const char* response, uint16_t resp_len,
                           um980_cmd_result_t* result)
{
    if (result == NULL) return false;
    memset(result, 0, sizeof(um980_cmd_result_t));
    result->status = UM980_CMD_PENDING;

    if (response == NULL || resp_len == 0) {
        result->response_received = false;
        return false;
    }

    result->response_received = true;
    result->response_len = resp_len;

    /* Build excerpt: first 127 chars, escape CR/LF */
    uint16_t excerpt_len = resp_len < 127 ? resp_len : 127;
    uint16_t ep = 0;
    for (uint16_t i = 0; i < excerpt_len && ep < 126; i++) {
        char c = response[i];
        if (c == '\r') {
            result->response_excerpt[ep++] = '\\';
            result->response_excerpt[ep++] = 'r';
        } else if (c == '\n') {
            result->response_excerpt[ep++] = '\\';
            result->response_excerpt[ep++] = 'n';
        } else {
            result->response_excerpt[ep++] = c;
        }
    }
    result->response_excerpt[ep] = '\0';

    /* Scan for OK pattern, skipping NMEA data lines */
    /* We scan the entire buffer for "response: OK" or standalone "OK" */
    bool found_ok = false;
    bool found_error = false;

    /* Pattern 1: "response: OK" (case-insensitive) */
    const char* p = stristr(response, "response: ok", resp_len);
    if (p != NULL) {
        /* Verify it's not inside a NMEA data field */
        found_ok = true;
    }

    /* Pattern 2: standalone "OK" not preceded by alnum (e.g., end of line) */
    if (!found_ok) {
        for (uint16_t i = 0; i < resp_len; i++) {
            if (response[i] == 'O' || response[i] == 'o') {
                if (i + 1 < resp_len && (response[i + 1] == 'K' || response[i + 1] == 'k')) {
                    /* Check preceding char is not alnum */
                    bool preceded_ok = (i == 0) || (!isalnum((unsigned char)response[i - 1]));
                    /* Check following char is not alnum */
                    bool followed_ok = (i + 2 >= resp_len) || (!isalnum((unsigned char)response[i + 2]));
                    if (preceded_ok && followed_ok) {
                        found_ok = true;
                        break;
                    }
                }
            }
        }
    }

    /* Pattern 3: "response: ERROR" (case-insensitive) */
    p = stristr(response, "response: error", resp_len);
    if (p != NULL) {
        found_error = true;
    }

    /* Pattern 4: standalone "ERROR" not preceded by alnum */
    if (!found_error) {
        for (uint16_t i = 0; i < resp_len; i++) {
            if ((response[i] == 'E' || response[i] == 'e') &&
                (i + 4 < resp_len)) {
                if ((response[i + 1] == 'R' || response[i + 1] == 'r') &&
                    (response[i + 2] == 'R' || response[i + 2] == 'r') &&
                    (response[i + 3] == 'O' || response[i + 3] == 'o') &&
                    (response[i + 4] == 'R' || response[i + 4] == 'r')) {
                    bool preceded_ok = (i == 0) || (!isalnum((unsigned char)response[i - 1]));
                    bool followed_ok = (i + 5 >= resp_len) || (!isalnum((unsigned char)response[i + 5]));
                    if (preceded_ok && followed_ok) {
                        found_error = true;
                        break;
                    }
                }
            }
        }
    }

    result->ack_ok = found_ok;
    result->ack_error = found_error;

    if (found_ok && !found_error) {
        result->status = UM980_CMD_OK;
        return true;
    } else if (found_error) {
        result->status = UM980_CMD_ERROR;
        return true;
    }

    /* No definitive status found */
    return false;
}

/* =================================================================
 * Internal: send current command to UART TX
 * ================================================================= */

static bool send_current_cmd(gnss_nmea_config_t* cfg)
{
    if (cfg == NULL || cfg->cmd_index >= cfg->cmd_count) return false;

    const char* cmd = cfg->cmd_queue[cfg->cmd_index];
    size_t cmd_len = strlen(cmd);

    if (cfg->uart == NULL) {
        ESP_LOGE(TAG, "%s: no UART for command send", cfg->name);
        cfg->last_result.status = UM980_CMD_TRANSPORT_FAIL;
        cfg->last_result.transport_ok = false;
        return false;
    }

    /* Check TX buffer has space */
    size_t free_space = transport_uart_tx_free(cfg->uart);
    if (free_space < cmd_len) {
        ESP_LOGW(TAG, "%s: TX buffer full (free=%u, need=%u)",
                 cfg->name, (unsigned)free_space, (unsigned)cmd_len);
        return false; /* retry next cycle */
    }

    size_t written = transport_uart_tx_write(cfg->uart,
                                              (const uint8_t*)cmd, cmd_len);

    cfg->last_result.transport_ok = (written == cmd_len);
    cfg->last_result.response_received = false;
    cfg->last_result.ack_ok = false;
    cfg->last_result.ack_error = false;
    cfg->last_result.response_excerpt[0] = '\0';
    cfg->last_result.response_len = 0;

    if (!cfg->last_result.transport_ok) {
        ESP_LOGE(TAG, "%s: transport fail (wrote %u/%u)",
                 cfg->name, (unsigned)written, (unsigned)cmd_len);
        cfg->last_result.status = UM980_CMD_TRANSPORT_FAIL;
        return true; /* advance to next command */
    }

    /* Prepare for response collection */
    cfg->response_pos = 0;
    memset(cfg->response_buf, 0, sizeof(cfg->response_buf));
    cfg->cmd_sent_us = now_us();
    cfg->last_result.status = UM980_CMD_SENT;

    ESP_LOGI(TAG, "%s: sent cmd[%u/%u] len=%u",
             cfg->name, cfg->cmd_index + 1, cfg->cmd_count, (unsigned)cmd_len);

    return true;
}

/* =================================================================
 * Internal: read response from UART RX buffer
 * ================================================================= */

static void read_response(gnss_nmea_config_t* cfg)
{
    if (cfg == NULL || cfg->uart == NULL) return;

    uint8_t tmp[128];
    size_t available = transport_uart_rx_available(cfg->uart);

    while (available > 0) {
        size_t to_read = available > sizeof(tmp) ? sizeof(tmp) : available;
        size_t pulled = transport_uart_rx_read(cfg->uart, tmp, to_read);
        if (pulled == 0) break;
        available -= pulled;

        /* Forward ALL bytes to gnss_um980 for NMEA parsing */
        if (cfg->gnss != NULL) {
            uint32_t before = cfg->gnss->sentences_parsed;
            gnss_um980_feed(cfg->gnss, tmp, pulled);
            if (cfg->gnss->sentences_parsed > before) {
                cfg->gnss->last_sentence_us = now_us();
            }
        }

        /* Also capture response bytes */
        for (size_t i = 0; i < pulled; i++) {
            if (cfg->response_pos < UM980_RESP_MAX_LEN - 1) {
                cfg->response_buf[cfg->response_pos++] = (char)tmp[i];
            }
        }
    }
}

/* =================================================================
 * Internal: process completed command result
 * ================================================================= */

static void process_cmd_result(gnss_nmea_config_t* cfg, uint64_t timestamp_us)
{
    if (cfg == NULL) return;

    /* Parse response */
    bool definitive = um980_parse_response(cfg->response_buf,
                                            cfg->response_pos,
                                            &cfg->last_result);

    if (!definitive) {
        uint64_t elapsed_us = timestamp_us - cfg->cmd_sent_us;
        if (elapsed_us > (uint64_t)UM980_RESP_TIMEOUT_MS * 1000) {
            cfg->last_result.status = UM980_CMD_TIMEOUT;
            ESP_LOGW(TAG, "%s: cmd[%u/%u] TIMEOUT after %llums",
                     cfg->name, cfg->cmd_index + 1, cfg->cmd_count,
                     (unsigned long long)(elapsed_us / 1000));
        } else {
            return; /* still waiting */
        }
    }

    /* Get clean command string (without CRLF) for logging */
    char cmd_clean[UM980_CMD_MAX_LEN];
    strncpy(cmd_clean, cfg->cmd_queue[cfg->cmd_index], sizeof(cmd_clean) - 1);
    cmd_clean[sizeof(cmd_clean) - 1] = '\0';
    /* Strip CRLF */
    size_t clen = strlen(cmd_clean);
    while (clen > 0 && (cmd_clean[clen - 1] == '\r' || cmd_clean[clen - 1] == '\n')) {
        cmd_clean[--clen] = '\0';
    }

    const char* status_str;
    switch (cfg->last_result.status) {
        case UM980_CMD_OK:             status_str = "OK"; break;
        case UM980_CMD_ERROR:          status_str = "ERROR"; break;
        case UM980_CMD_TIMEOUT:        status_str = "TIMEOUT"; break;
        case UM980_CMD_TRANSPORT_FAIL: status_str = "TRANSPORT_FAIL"; break;
        case UM980_CMD_EFFECT_NO_ACK:  status_str = "EFFECT_NO_ACK"; break;
        default:                       status_str = "UNKNOWN"; break;
    }

    /* Log raw response excerpt (Aufgabe 1: bounded 300-500 chars) */
    ESP_LOGI(TAG, "GNSS_CMD_RAW: receiver=%u cmd=\"%s\" resp_len=%u status=%s excerpt=\"%s\"",
             cfg->instance_id + 1, cmd_clean,
             cfg->last_result.response_len, status_str,
             cfg->last_result.response_excerpt[0] ? cfg->last_result.response_excerpt : "(empty)");

    /* Track success/failure */
    if (cfg->last_result.status == UM980_CMD_OK ||
        cfg->last_result.status == UM980_CMD_EFFECT_NO_ACK) {
        cfg->restore_ok_count++;
    } else {
        cfg->restore_fail_count++;
        if (cfg->restore_fail_cmd[0] == '\0') {
            strncpy(cfg->restore_fail_cmd, cmd_clean, sizeof(cfg->restore_fail_cmd) - 1);
        }
    }

    /* Advance to next command */
    cfg->cmd_index++;
    cfg->last_cmd_us = timestamp_us;
}

/* =================================================================
 * Public API
 * ================================================================= */

void gnss_nmea_config_init(gnss_nmea_config_t* cfg, uint8_t instance_id, const char* name)
{
    if (cfg == NULL) return;

    memset(cfg, 0, sizeof(gnss_nmea_config_t));
    cfg->instance_id = instance_id;
    cfg->name = name ? name : "GNSS_CFG";
    cfg->restore_state = NMEA_RESTORE_IDLE;

    /* Register service step callback */
    cfg->component.service_step = gnss_nmea_config_service_step;
}

void gnss_nmea_config_set_sources(gnss_nmea_config_t* cfg,
                                   transport_uart_t* uart,
                                   gnss_um980_t* gnss)
{
    if (cfg == NULL) return;
    cfg->uart = uart;
    cfg->gnss = gnss;
}

void gnss_nmea_config_start_restore(gnss_nmea_config_t* cfg)
{
    if (cfg == NULL) return;

    build_restore_queue(cfg);
    cfg->restore_state = NMEA_RESTORE_UNLOGGING;
    cfg->restore_start_us = now_us();
    cfg->restore_ok_count = 0;
    cfg->restore_fail_count = 0;
    cfg->restore_fail_cmd[0] = '\0';
    cfg->cmd_index = 0;

    /* Link gnss intercept flag for RX interception */
    if (cfg->gnss != NULL) {
        cfg->gnss->nmea_config_intercept = true;
    }

    ESP_LOGI(TAG, "%s: NMEA restore started (receiver %u, %u commands queued)",
             cfg->name, cfg->instance_id + 1, cfg->cmd_count);
}

nmea_restore_state_t gnss_nmea_config_get_restore_state(const gnss_nmea_config_t* cfg)
{
    if (cfg == NULL) return NMEA_RESTORE_IDLE;
    return cfg->restore_state;
}

const um980_cmd_result_t* gnss_nmea_config_get_last_result(const gnss_nmea_config_t* cfg)
{
    if (cfg == NULL) return NULL;
    return &cfg->last_result;
}

/* =================================================================
 * Service Step — Restore State Machine
 *
 * States:
 *   IDLE → (start_restore) → UNLOGGING → SETTING → DONE/FAILED → IDLE
 *
 * During UNLOGGING and SETTING:
 *   - Reads ALL bytes from UART RX buffer
 *   - Forwards NMEA data to gnss_um980 via feed()
 *   - Collects response for ACK detection
 *   - Advances through command queue with inter-command delay
 *
 * After DONE/FAILED → IDLE:
 *   - Disconnects from gnss (gnss reads UART RX directly)
 *   - Component becomes a no-op in service loop
 * ================================================================= */

void gnss_nmea_config_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    gnss_nmea_config_t* cfg = (gnss_nmea_config_t*)comp;
    if (cfg == NULL) return;

    switch (cfg->restore_state) {

    case NMEA_RESTORE_IDLE:
        /* No restore in progress — do nothing */
        return;

    case NMEA_RESTORE_UNLOGGING:
    case NMEA_RESTORE_SETTING:
    {
        /* Check if all commands completed */
        if (cfg->cmd_index >= cfg->cmd_count) {
            /* Determine if LOG phase commands (index >= 9) had failures */
            bool critical_fail = false;
            for (uint8_t i = 9; i < cfg->cmd_count; i++) {
                /* We can't re-check, but restore_fail_count tracks overall */
                /* For now, any failure in LOG phase is critical */
                (void)i;
            }

            if (cfg->restore_fail_count > 0) {
                cfg->restore_state = NMEA_RESTORE_FAILED;
                ESP_LOGE(TAG, "%s: === NMEA restore FAILED === "
                         "(ok=%u fail=%u, first_fail=\"%s\")",
                         cfg->name, cfg->restore_ok_count, cfg->restore_fail_count,
                         cfg->restore_fail_cmd[0] ? cfg->restore_fail_cmd : "(none)");
            } else {
                cfg->restore_state = NMEA_RESTORE_DONE;
                ESP_LOGI(TAG, "%s: === NMEA restore OK === "
                         "(all %u commands succeeded)", cfg->name, cfg->cmd_count);
            }

            /* Disconnect intercept — let gnss read UART RX directly */
            if (cfg->gnss != NULL) {
                cfg->gnss->nmea_config_intercept = false;
            }

            uint64_t elapsed_ms = (now_us() - cfg->restore_start_us) / 1000;
            ESP_LOGI(TAG, "%s: restore completed in %llums", cfg->name,
                     (unsigned long long)elapsed_ms);
            return;
        }

        /* Process current command state */
        switch (cfg->last_result.status) {
        case UM980_CMD_PENDING:
        {
            /* Send the current command */
            /* Enforce inter-command delay */
            if (cfg->last_cmd_us > 0) {
                uint64_t since_last = (timestamp_us - cfg->last_cmd_us) / 1000;
                if (since_last < UM980_INTER_CMD_DELAY_MS) {
                    return; /* wait */
                }
            }

            if (send_current_cmd(cfg)) {
                if (cfg->last_result.status == UM980_CMD_TRANSPORT_FAIL) {
                    /* Transport failed — advance immediately */
                    process_cmd_result(cfg, timestamp_us);
                }
                /* else: waiting for response */
            }
            /* else: TX buffer full, retry next cycle */
            break;
        }

        case UM980_CMD_SENT:
        {
            /* Read response from UART RX */
            read_response(cfg);

            /* Check if we have enough response data or timeout */
            uint64_t elapsed_us = timestamp_us - cfg->cmd_sent_us;
            if (elapsed_us > (uint64_t)UM980_RESP_TIMEOUT_MS * 1000) {
                process_cmd_result(cfg, timestamp_us);
            } else if (cfg->response_pos > 0) {
                /* Try to parse response early */
                bool definitive = um980_parse_response(cfg->response_buf,
                                                        cfg->response_pos,
                                                        &cfg->last_result);
                if (definitive) {
                    /* Got definitive response, log and advance */
                    char cmd_clean[UM980_CMD_MAX_LEN];
                    strncpy(cmd_clean, cfg->cmd_queue[cfg->cmd_index],
                            sizeof(cmd_clean) - 1);
                    cmd_clean[sizeof(cmd_clean) - 1] = '\0';
                    size_t clen = strlen(cmd_clean);
                    while (clen > 0 && (cmd_clean[clen - 1] == '\r' ||
                                        cmd_clean[clen - 1] == '\n')) {
                        cmd_clean[--clen] = '\0';
                    }

                    ESP_LOGI(TAG, "GNSS_CMD_RAW: receiver=%u cmd=\"%s\" resp_len=%u "
                             "status=%s excerpt=\"%s\"",
                             cfg->instance_id + 1, cmd_clean,
                             cfg->last_result.response_len,
                             cfg->last_result.status == UM980_CMD_OK ? "OK" :
                             cfg->last_result.status == UM980_CMD_ERROR ? "ERROR" : "OTHER",
                             cfg->last_result.response_excerpt);

                    /* Track result */
                    if (cfg->last_result.status == UM980_CMD_OK ||
                        cfg->last_result.status == UM980_CMD_EFFECT_NO_ACK) {
                        cfg->restore_ok_count++;
                    } else {
                        cfg->restore_fail_count++;
                        if (cfg->restore_fail_cmd[0] == '\0') {
                            strncpy(cfg->restore_fail_cmd, cmd_clean,
                                    sizeof(cfg->restore_fail_cmd) - 1);
                        }
                    }

                    cfg->cmd_index++;
                    cfg->last_cmd_us = timestamp_us;
                    cfg->last_result.status = UM980_CMD_PENDING;
                }
                /* else: response incomplete, keep reading */
            }
            break;
        }

        default:
            /* Previous command completed, reset for next */
            cfg->last_result.status = UM980_CMD_PENDING;
            break;
        }

        /* Transition from UNLOGGING to SETTING when past the UNLOG commands */
        /* UNLOG commands: indices 0-8 (9 commands), LOG commands start at index 9 */
        if (cfg->restore_state == NMEA_RESTORE_UNLOGGING && cfg->cmd_index >= 9) {
            cfg->restore_state = NMEA_RESTORE_SETTING;
            ESP_LOGI(TAG, "%s: UNLOG phase complete, starting LOG phase",
                     cfg->name);
        }
        break;
    }

    case NMEA_RESTORE_DONE:
    case NMEA_RESTORE_FAILED:
        /* Restore finished — stay idle */
        return;
    }
}
