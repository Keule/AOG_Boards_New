/* ========================================================================
 * gnss_nmea_config.c — UM980 NMEA Configuration Manager Implementation
 *                        (NAV-UART-STABILIZING-R3)
 *
 * STATE MACHINE DESIGN (corrected):
 *   Each command flows through EXACTLY these states:
 *     IDLE → SENDING → SENT → COLLECTING → (OK|ERROR|TIMEOUT|TRANSPORT_FAIL) → DONE
 *
 *   cmd_index is incremented in EXACTLY ONE place: process_cmd_result()
 *   No other code path may modify cmd_index.
 *
 *   After all commands complete, VERIFYING state observes actual NMEA
 *   rates for VERIFY_WINDOW_MS before reporting DONE/FAILED.
 *
 * R3 CHANGES:
 *   - C1: process_cmd_result no longer overwrites status before logging.
 *         Original UM980_CMD_OK/ERROR/TIMEOUT is preserved and logged.
 *   - B1: New effect flags NO_NMEA_OUTPUT, GGA_MISSING, RMC_MISSING,
 *         GSA_MISSING. 0 Hz GGA/RMC/GSA now causes RESTORE_FAILED.
 *   - B2: GSA_HIGH added to non-blocking WARN class (not silent OK).
 *   - B3: Text flags printed alongside hex flags in restore log.
 *   - C1: ok/fail counting uses saved original status, not overwritten DONE.
 * ======================================================================== */

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>

#include "gnss_nmea_config.h"

/* ---- Component headers (required for type access in implementation) ---- */
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

/* =================================================================
 * INTERNAL: Command result reset
 *
 * Called BEFORE each new command to prevent inheriting state from
 * the previous command (fixes Task #2).
 * ================================================================= */

static void reset_cmd_result(gnss_nmea_config_t* cfg)
{
    if (cfg == NULL) return;
    memset(&cfg->last_result, 0, sizeof(um980_cmd_result_t));
    cfg->last_result.status = UM980_CMD_IDLE;
    cfg->response_pos = 0;
    memset(cfg->response_buf, 0, sizeof(cfg->response_buf));
}

/* =================================================================
 * INTERNAL: Queue a command string
 * ================================================================= */

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

/* =================================================================
 * INTERNAL: Helper — build a command string with the receiver's UM980 port
 *
 * Uses printf-style formatting so callers can pass cfg->um980_port
 * as a %s argument instead of relying on string concatenation with
 * the UM980_PORT_COM2 macro.  This makes each receiver's port
 * independently configurable (Aufgabe 3).
 * ================================================================= */

static void queue_port_cmd(gnss_nmea_config_t* cfg, const char* fmt, ...)
{
    if (cfg == NULL) return;
    char full_cmd[UM980_CMD_MAX_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(full_cmd, sizeof(full_cmd), fmt, ap);
    va_end(ap);
    queue_cmd(cfg, full_cmd);
}

/* =================================================================
 * INTERNAL: Build restore command queue
 *
 * Phase 1: UNLOG ALL (clear every sentence type from the receiver's port)
 *   - Broad UNLOG <port> (clear port)
 *   - Per-sentence-type UNLOG for all talker IDs (GNGGA, GNRMC, etc.)
 *   - Extra GSV talker IDs: GN, GP, GA, GB (Task #6)
 *
 * Phase 2: LOG target profile
 *   - Uses LOG <port> <sentence> ONTIME <period> syntax (ADR decision)
 *   - GST: only if profile has non-zero period, else skipped
 *   - GSV: explicitly NOT enabled
 *
 * Aufgabe 3: All commands use cfg->um980_port (default "COM2")
 *   so each receiver can be independently configured.
 * ================================================================= */

static void build_restore_queue(gnss_nmea_config_t* cfg)
{
    cfg->cmd_count = 0;
    cfg->cmd_index = 0;

    /* ---- Phase 1: UNLOG all ---- */

    /* Broad port clear */
    queue_port_cmd(cfg, "UNLOG %s", cfg->um980_port);

    /* Per-sentence-type UNLOG — Navigation sentences */
    queue_port_cmd(cfg, "UNLOG %s GNGGA", cfg->um980_port);
    queue_port_cmd(cfg, "UNLOG %s GNRMC", cfg->um980_port);
    queue_port_cmd(cfg, "UNLOG %s GNGST", cfg->um980_port);
    queue_port_cmd(cfg, "UNLOG %s GNGSA", cfg->um980_port);

    /* Per-sentence-type UNLOG — GSV all talker IDs (Task #6) */
    queue_port_cmd(cfg, "UNLOG %s GNGSV", cfg->um980_port);
    queue_port_cmd(cfg, "UNLOG %s GPGSV", cfg->um980_port);
    queue_port_cmd(cfg, "UNLOG %s GAGSV", cfg->um980_port);
    queue_port_cmd(cfg, "UNLOG %s GBGSV", cfg->um980_port);

    /* ---- Phase 2: LOG target profile ---- */

    char period_buf[16];

    /* GGA */
    if (cfg->target_profile.gga_period_s > 0.0f) {
        snprintf(period_buf, sizeof(period_buf), "%.2f", cfg->target_profile.gga_period_s);
        queue_port_cmd(cfg, "LOG %s GNGGA ONTIME %s", cfg->um980_port, period_buf);
    }

    /* RMC */
    if (cfg->target_profile.rmc_period_s > 0.0f) {
        snprintf(period_buf, sizeof(period_buf), "%.2f", cfg->target_profile.rmc_period_s);
        queue_port_cmd(cfg, "LOG %s GNRMC ONTIME %s", cfg->um980_port, period_buf);
    }

    /* GST — only if profile requests it (Task #7) */
    if (cfg->target_profile.gst_period_s > 0.0f) {
        snprintf(period_buf, sizeof(period_buf), "%.2f", cfg->target_profile.gst_period_s);
        queue_port_cmd(cfg, "LOG %s GNGST ONTIME %s", cfg->um980_port, period_buf);
    }

    /* GSA */
    if (cfg->target_profile.gsa_period_s > 0.0f) {
        snprintf(period_buf, sizeof(period_buf), "%.1f", cfg->target_profile.gsa_period_s);
        queue_port_cmd(cfg, "LOG %s GNGSA ONTIME %s", cfg->um980_port, period_buf);
    }

    /* GSV: explicitly NOT enabled (Task #6) */

    ESP_LOGI(TAG, "%s: restore queue built with %u commands (port=%s profile: GGA=%.1fHz RMC=%.1fHz GST=%s GSA=%.1fHz GSV=off)",
             cfg->name, cfg->cmd_count, cfg->um980_port,
             cfg->target_profile.gga_period_s > 0 ? 1.0f / cfg->target_profile.gga_period_s : 0.0f,
             cfg->target_profile.rmc_period_s > 0 ? 1.0f / cfg->target_profile.rmc_period_s : 0.0f,
             cfg->target_profile.gst_period_s > 0 ? "on" : "OFF(GST_NOT_AVAIL)",
             cfg->target_profile.gsa_period_s > 0 ? 1.0f / cfg->target_profile.gsa_period_s : 0.0f);
}

/* =================================================================
 * ACK Detection — um980_parse_response()
 *
 * Scans response buffer for UM980 acknowledgment patterns.
 * Pure function: does not modify any component state.
 * ================================================================= */

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

bool um980_parse_response(const char* response, uint16_t resp_len,
                           um980_cmd_result_t* result)
{
    if (result == NULL) return false;
    /* NOTE: We only SET ack_ok/ack_error and response fields here.
     * We do NOT set status — the caller decides the final status.
     * This prevents double-processing. */
    result->ack_ok = false;
    result->ack_error = false;

    if (response == NULL || resp_len == 0) {
        result->response_received = false;
        return false;
    }

    result->response_received = true;
    result->response_len = resp_len;

    /* Build excerpt */
    uint16_t excerpt_len = resp_len < 127 ? resp_len : 127;
    uint16_t ep = 0;
    for (uint16_t i = 0; i < excerpt_len && ep < 126; i++) {
        char c = response[i];
        if (c == '\r') { result->response_excerpt[ep++] = '\\'; result->response_excerpt[ep++] = 'r'; }
        else if (c == '\n') { result->response_excerpt[ep++] = '\\'; result->response_excerpt[ep++] = 'n'; }
        else { result->response_excerpt[ep++] = c; }
    }
    result->response_excerpt[ep] = '\0';

    /* Pattern 1: "response: ok" (case-insensitive) */
    if (stristr(response, "response: ok", resp_len) != NULL) {
        result->ack_ok = true;
    }

    /* Pattern 2: standalone "OK" with word boundary */
    if (!result->ack_ok) {
        for (uint16_t i = 0; i < resp_len; i++) {
            if ((response[i] == 'O' || response[i] == 'o') &&
                (i + 1 < resp_len) && (response[i + 1] == 'K' || response[i + 1] == 'k')) {
                bool pre = (i == 0) || (!isalnum((unsigned char)response[i - 1]));
                bool post = (i + 2 >= resp_len) || (!isalnum((unsigned char)response[i + 2]));
                if (pre && post) { result->ack_ok = true; break; }
            }
        }
    }

    /* Pattern 3: "response: error" */
    if (stristr(response, "response: error", resp_len) != NULL) {
        result->ack_error = true;
    }

    /* Pattern 4: standalone "ERROR" with word boundary */
    if (!result->ack_error) {
        for (uint16_t i = 0; i + 4 < resp_len; i++) {
            if ((response[i] == 'E' || response[i] == 'e') &&
                (response[i+1]=='R'||response[i+1]=='r') && (response[i+2]=='R'||response[i+2]=='r') &&
                (response[i+3]=='O'||response[i+3]=='o') && (response[i+4]=='R'||response[i+4]=='r')) {
                bool pre = (i == 0) || (!isalnum((unsigned char)response[i - 1]));
                bool post = (i + 5 >= resp_len) || (!isalnum((unsigned char)response[i + 5]));
                if (pre && post) { result->ack_error = true; break; }
            }
        }
    }

    return (result->ack_ok || result->ack_error);
}

/* =================================================================
 * INTERNAL: Effect flags to text (R3 B3)
 * ================================================================= */

static void append_flag(char* buf, size_t bufsz, size_t* pos, const char* name, bool set)
{
    if (!set || buf == NULL || pos == NULL) return;
    if (*pos > 0 && *pos < bufsz - 1) {
        buf[(*pos)++] = '|';
    }
    size_t nlen = strlen(name);
    if (*pos + nlen < bufsz - 1) {
        memcpy(buf + *pos, name, nlen);
        *pos += nlen;
    }
}

static const char* effect_flags_text(uint32_t flags, char* buf, size_t bufsz)
{
    if (buf == NULL || bufsz == 0) return "";
    size_t pos = 0;
    buf[0] = '\0';
    append_flag(buf, bufsz, &pos, "ACK_OK",                   !!(flags & RESTORE_EFFECT_ACK_OK));
    append_flag(buf, bufsz, &pos, "NO_ACK_BUT_OK",            !!(flags & RESTORE_EFFECT_ACK_MISSING_BUT_EFFECT_OK));
    append_flag(buf, bufsz, &pos, "ACK_OK_EFFECT_BAD",        !!(flags & RESTORE_EFFECT_ACK_OK_BUT_EFFECT_BAD));
    append_flag(buf, bufsz, &pos, "TIMEOUT",                  !!(flags & RESTORE_EFFECT_TIMEOUT));
    append_flag(buf, bufsz, &pos, "GSV_ACTIVE",              !!(flags & RESTORE_EFFECT_GSV_STILL_ACTIVE));
    append_flag(buf, bufsz, &pos, "GST_NA",                   !!(flags & RESTORE_EFFECT_GST_NOT_AVAILABLE));
    append_flag(buf, bufsz, &pos, "RATE_HIGH",                !!(flags & RESTORE_EFFECT_RATE_TOO_HIGH));
    append_flag(buf, bufsz, &pos, "RATE_LOW",                 !!(flags & RESTORE_EFFECT_RATE_TOO_LOW));
    append_flag(buf, bufsz, &pos, "UART_OVERRUN",             !!(flags & RESTORE_EFFECT_UART_OVERRUN));
    append_flag(buf, bufsz, &pos, "RING_FULL",                !!(flags & RESTORE_EFFECT_RING_FULL));
    append_flag(buf, bufsz, &pos, "GSA_HIGH",                 !!(flags & RESTORE_EFFECT_GSA_HIGH));
    append_flag(buf, bufsz, &pos, "NO_NMEA_OUTPUT",           !!(flags & RESTORE_EFFECT_NO_NMEA_OUTPUT));
    append_flag(buf, bufsz, &pos, "GGA_MISSING",              !!(flags & RESTORE_EFFECT_GGA_MISSING));
    append_flag(buf, bufsz, &pos, "RMC_MISSING",              !!(flags & RESTORE_EFFECT_RMC_MISSING));
    append_flag(buf, bufsz, &pos, "GSA_MISSING",              !!(flags & RESTORE_EFFECT_GSA_MISSING));
    append_flag(buf, bufsz, &pos, "GSA_MULTI_TALKER",         !!(flags & RESTORE_EFFECT_GSA_MULTI_TALKER));
    append_flag(buf, bufsz, &pos, "GST_UNEXPECTED",           !!(flags & RESTORE_EFFECT_GST_UNEXPECTED));
    append_flag(buf, bufsz, &pos, "GST_DISABLED",             !!(flags & RESTORE_EFFECT_GST_DISABLED));
    buf[pos] = '\0';
    return (pos > 0) ? buf : "NONE";
}

/* =================================================================
 * INTERNAL: Log command result
 * ================================================================= */

static void log_cmd_result(const gnss_nmea_config_t* cfg)
{
    if (cfg == NULL) return;

    /* Get clean command string */
    char cmd_clean[UM980_CMD_MAX_LEN];
    strncpy(cmd_clean, cfg->cmd_queue[cfg->cmd_index], sizeof(cmd_clean) - 1);
    cmd_clean[sizeof(cmd_clean) - 1] = '\0';
    size_t clen = strlen(cmd_clean);
    while (clen > 0 && (cmd_clean[clen - 1] == '\r' || cmd_clean[clen - 1] == '\n')) {
        cmd_clean[--clen] = '\0';
    }

    const char* status_str;
    switch (cfg->last_result.status) {
        case UM980_CMD_OK:            status_str = "OK"; break;
        case UM980_CMD_ERROR:         status_str = "ERROR"; break;
        case UM980_CMD_TIMEOUT:       status_str = "TIMEOUT"; break;
        case UM980_CMD_TRANSPORT_FAIL: status_str = "TRANSPORT_FAIL"; break;
        default:                      status_str = "UNKNOWN"; break;
    }

    ESP_LOGI(TAG, "GNSS_CMD_RAW: receiver=%u cmd=\"%s\" resp_len=%u status=%s excerpt=\"%s\"",
             cfg->instance_id + 1, cmd_clean,
             cfg->last_result.response_len, status_str,
             cfg->last_result.response_excerpt[0] ? cfg->last_result.response_excerpt : "(empty)");
}

/* =================================================================
 * INTERNAL: process_cmd_result
 *
 * SINGLE EXIT POINT for command completion.
 * This is the ONLY function that may increment cmd_index.
 * Every command — ACK_OK, ACK_ERROR, TIMEOUT, TRANSPORT_FAIL —
 * flows through this function exactly once.
 *
 * R3 FIX (C1): Save original status BEFORE overwriting to DONE.
 * This preserves the real command outcome for ok/fail counting
 * and for correct logging.
 * ================================================================= */

static void process_cmd_result(gnss_nmea_config_t* cfg, uint64_t timestamp_us)
{
    if (cfg == NULL) return;
    if (cfg->cmd_index >= cfg->cmd_count) return;

    /* R3 C1: Save the ORIGINAL status before overwriting */
    um980_cmd_status_t original_status = cfg->last_result.status;

    /* Mark completion time */
    cfg->last_result.completed_us = timestamp_us;

    /* R3 C1: Log BEFORE overwriting status to DONE */
    log_cmd_result(cfg);

    /* Track success/failure using ORIGINAL status (R3 C1 fix) */
    bool is_ok = (original_status == UM980_CMD_OK);
    bool is_fail = (original_status == UM980_CMD_ERROR ||
                   original_status == UM980_CMD_TIMEOUT ||
                   original_status == UM980_CMD_TRANSPORT_FAIL);

    if (is_ok) {
        cfg->restore_ok_count++;
    } else if (is_fail) {
        cfg->restore_fail_count++;
        if (cfg->restore_fail_cmd[0] == '\0') {
            char cmd_clean[UM980_CMD_MAX_LEN];
            strncpy(cmd_clean, cfg->cmd_queue[cfg->cmd_index],
                    sizeof(cmd_clean) - 1);
            cmd_clean[sizeof(cmd_clean) - 1] = '\0';
            size_t clen = strlen(cmd_clean);
            while (clen > 0 && (cmd_clean[clen - 1] == '\r' || cmd_clean[clen - 1] == '\n')) {
                cmd_clean[--clen] = '\0';
            }
            strncpy(cfg->restore_fail_cmd, cmd_clean,
                    sizeof(cfg->restore_fail_cmd) - 1);
            cfg->restore_fail_cmd[sizeof(cfg->restore_fail_cmd) - 1] = '\0';
        }
    }

    /* NOW overwrite status to DONE (after logging and counting) */
    cfg->last_result.status = UM980_CMD_DONE;

    /* ==============================================
     * SINGLE PLACE WHERE cmd_index IS INCREMENTED
     * ============================================== */
    cfg->cmd_index++;
    cfg->last_cmd_us = timestamp_us;
}

/* =================================================================
 * INTERNAL: send_current_cmd
 *
 * Resets result, writes to TX, sets status to SENT.
 * Does NOT advance cmd_index.
 * ================================================================= */

static bool send_current_cmd(gnss_nmea_config_t* cfg)
{
    if (cfg == NULL || cfg->cmd_index >= cfg->cmd_count) return false;

    /* Reset result before sending (Task #2) */
    reset_cmd_result(cfg);

    const char* cmd = cfg->cmd_queue[cfg->cmd_index];
    size_t cmd_len = strlen(cmd);

    if (cfg->uart == NULL) {
        ESP_LOGE(TAG, "%s: no UART for command send", cfg->name);
        cfg->last_result.status = UM980_CMD_TRANSPORT_FAIL;
        cfg->last_result.completed_us = now_us();
        return true; /* signal completion to caller */
    }

    size_t free_space = transport_uart_tx_free(cfg->uart);
    if (free_space < cmd_len) {
        return false; /* TX full, retry next cycle */
    }

    size_t written = transport_uart_tx_write(cfg->uart,
                                              (const uint8_t*)cmd, cmd_len);
    cfg->last_result.started_us = now_us();

    if (written != cmd_len) {
        ESP_LOGE(TAG, "%s: partial write %u/%u", cfg->name,
                 (unsigned)written, (unsigned)cmd_len);
        cfg->last_result.transport_ok = false;
        cfg->last_result.status = UM980_CMD_TRANSPORT_FAIL;
        cfg->last_result.completed_us = now_us();
        return true; /* signal completion */
    }

    cfg->last_result.transport_ok = true;
    cfg->last_result.status = UM980_CMD_SENT;
    cfg->cmd_sent_us = now_us();
    cfg->response_pos = 0;

    ESP_LOGI(TAG, "%s: sent cmd[%u/%u] len=%u",
             cfg->name, cfg->cmd_index + 1, cfg->cmd_count, (unsigned)cmd_len);

    return false; /* not yet complete — waiting for response */
}

/* =================================================================
 * INTERNAL: read_response
 *
 * Reads ALL bytes from UART RX, forwards to gnss_um980 via feed(),
 * captures response bytes for ACK detection.
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

        /* Capture response bytes */
        for (size_t i = 0; i < pulled; i++) {
            if (cfg->response_pos < UM980_RESP_MAX_LEN - 1) {
                cfg->response_buf[cfg->response_pos++] = (char)tmp[i];
            }
        }
    }
}

/* =================================================================
 * INTERNAL: evaluate_effect
 *
 * After all commands complete, observe actual NMEA rates and
 * classify the restore effect (Task #4).
 * ================================================================= */

static void evaluate_effect(gnss_nmea_config_t* cfg)
{
    if (cfg == NULL || cfg->gnss == NULL) return;

    cfg->effect_flags = RESTORE_EFFECT_NONE;

    /* Snapshot current rates from gnss_um980 */
    cfg->verify_gga_hz = cfg->gnss->nmea_rate.rates_hz[0]; /* GGA */
    cfg->verify_rmc_hz = cfg->gnss->nmea_rate.rates_hz[1]; /* RMC */
    cfg->verify_gst_hz = cfg->gnss->nmea_rate.rates_hz[2]; /* GST */
    cfg->verify_gsa_hz = cfg->gnss->nmea_rate.rates_hz[3]; /* GSA */
    cfg->verify_gsv_hz = cfg->gnss->nmea_rate.rates_hz[4]; /* GSV */

    /* UART overrun check */
    const transport_uart_stats_t* stats = transport_uart_get_stats(cfg->uart);
    if (stats != NULL) {
        cfg->verify_overruns_end = stats->rx_overflow_count;
        if (cfg->verify_overruns_end > cfg->verify_overruns_start) {
            cfg->effect_flags |= RESTORE_EFFECT_UART_OVERRUN;
        }
    }

    /* Ring full check */
    uint32_t ring_used = transport_uart_rx_ring_used(cfg->uart);
    uint32_t ring_cap = transport_uart_rx_ring_capacity(cfg->uart);
    if (ring_cap > 0 && ring_used >= ring_cap * 90 / 100) {
        cfg->effect_flags |= RESTORE_EFFECT_RING_FULL;
    }

    /* GSV check (Task #6) */
    if (cfg->verify_gsv_hz > 0.5f) {
        cfg->effect_flags |= RESTORE_EFFECT_GSV_STILL_ACTIVE;
        cfg->gsv_was_disabled = false;
    } else {
        cfg->gsv_was_disabled = true;
    }

    /* GST check — R6: distinguish disabled/unexpected/available/unavailable */
    if (cfg->target_profile.gst_period_s > 0.0f) {
        /* GST was requested — check if it's actually arriving */
        if (cfg->verify_gst_hz < 0.5f) {
            cfg->effect_flags |= RESTORE_EFFECT_GST_NOT_AVAILABLE;
            cfg->gst_is_available = false;
        } else {
            cfg->gst_is_available = true;
        }
    } else {
        /* GST was NOT requested */
        cfg->gst_is_available = false;
        if (cfg->verify_gst_hz >= 0.5f) {
            /* GST disabled in profile but still in stream — unexpected */
            cfg->effect_flags |= RESTORE_EFFECT_GST_UNEXPECTED;
        } else {
            /* GST correctly absent */
            cfg->effect_flags |= RESTORE_EFFECT_GST_DISABLED;
        }
    }

    /* R3 B1: No-NMEA-output detection — all critical rates near 0 Hz */
    bool gga_zero = (cfg->target_profile.gga_period_s > 0.0f) &&
                    (cfg->verify_gga_hz < 0.5f);
    bool rmc_zero = (cfg->target_profile.rmc_period_s > 0.0f) &&
                    (cfg->verify_rmc_hz < 0.5f);
    bool gsa_zero = (cfg->target_profile.gsa_period_s > 0.0f) &&
                    (cfg->verify_gsa_hz < 0.5f);
    bool all_zero = gga_zero && rmc_zero && gsa_zero;

    if (all_zero) {
        cfg->effect_flags |= RESTORE_EFFECT_NO_NMEA_OUTPUT;
    }
    if (gga_zero) {
        cfg->effect_flags |= RESTORE_EFFECT_GGA_MISSING;
    }
    if (rmc_zero) {
        cfg->effect_flags |= RESTORE_EFFECT_RMC_MISSING;
    }
    if (gsa_zero) {
        cfg->effect_flags |= RESTORE_EFFECT_GSA_MISSING;
    }

    /* GSA check — detect multi-talker (multiple constellations) */
    if (cfg->target_profile.gsa_period_s > 0.0f) {
        float expected_gsa = 1.0f / cfg->target_profile.gsa_period_s;
        if (cfg->verify_gsa_hz > expected_gsa * 2.0f) {
            /* Check if the ratio suggests whole-number multi-talker:
             * e.g., 1 Hz epoch with 3 constellations = 3 Hz sentence rate.
             * Allow 20% tolerance for rate measurement variance. */
            float ratio = cfg->verify_gsa_hz / expected_gsa;
            int whole_talkers = (int)(ratio + 0.5f);  /* round to nearest int */
            if (whole_talkers >= 2 &&
                ratio >= (float)whole_talkers * 0.8f &&
                ratio <= (float)whole_talkers * 1.2f) {
                /* Multi-talker pattern detected */
                cfg->effect_flags |= RESTORE_EFFECT_GSA_MULTI_TALKER;
            } else {
                /* Genuine rate anomaly */
                cfg->effect_flags |= RESTORE_EFFECT_GSA_HIGH;
            }
        }
        /* R3: Removed RATE_TOO_LOW for GSA — it's covered by GSA_MISSING above */
    }

    /* Rate too high check */
    float expected_gga = (cfg->target_profile.gga_period_s > 0) ?
                          1.0f / cfg->target_profile.gga_period_s : 0;
    float expected_rmc = (cfg->target_profile.rmc_period_s > 0) ?
                          1.0f / cfg->target_profile.rmc_period_s : 0;

    if (cfg->verify_gga_hz > expected_gga * 2.5f && expected_gga > 0) {
        cfg->effect_flags |= RESTORE_EFFECT_RATE_TOO_HIGH;
    }
    if (cfg->verify_rmc_hz > expected_rmc * 2.5f && expected_rmc > 0) {
        cfg->effect_flags |= RESTORE_EFFECT_RATE_TOO_HIGH;
    }

    /* ACK-based flags */
    if (cfg->restore_ok_count == cfg->cmd_count) {
        cfg->effect_flags |= RESTORE_EFFECT_ACK_OK;
    } else if (cfg->restore_fail_count > 0) {
        cfg->effect_flags |= RESTORE_EFFECT_TIMEOUT;
    }

    /* R6: GSA_MULTI_TALKER is informational, not a failure.
     * GST_UNEXPECTED and GST_DISABLED are also informational. */
    bool effect_bad = (cfg->effect_flags & (RESTORE_EFFECT_GSV_STILL_ACTIVE |
                                              RESTORE_EFFECT_UART_OVERRUN |
                                              RESTORE_EFFECT_RING_FULL)) != 0;

    bool effect_acceptable = !effect_bad;

    if (cfg->restore_ok_count == cfg->cmd_count && effect_acceptable) {
        /* ACK_OK and effect is good */
    } else if (effect_acceptable && cfg->restore_fail_count > 0) {
        /* Some ACKs missing but effect is OK */
        cfg->effect_flags |= RESTORE_EFFECT_ACK_MISSING_BUT_EFFECT_OK;
    } else if (!effect_acceptable && cfg->restore_ok_count == cfg->cmd_count) {
        /* ACK OK but effect is bad */
        cfg->effect_flags |= RESTORE_EFFECT_ACK_OK_BUT_EFFECT_BAD;
    }

    /* R3 B3: Log effect flags with text representation */
    char flag_text[256];
    ESP_LOGI(TAG, "%s: EFFECT_VERIFY gga=%.1f rmc=%.1f gst=%.1f gsa=%.1f gsv=%.1f flags=0x%04x [%s]",
             cfg->name,
             cfg->verify_gga_hz, cfg->verify_rmc_hz, cfg->verify_gst_hz,
             cfg->verify_gsa_hz, cfg->verify_gsv_hz,
             cfg->effect_flags,
             effect_flags_text(cfg->effect_flags, flag_text, sizeof(flag_text)));
}

/* =================================================================
 * PUBLIC API
 * ================================================================= */

void gnss_nmea_config_init(gnss_nmea_config_t* cfg, uint8_t instance_id, const char* name)
{
    if (cfg == NULL) return;
    memset(cfg, 0, sizeof(gnss_nmea_config_t));
    cfg->instance_id = instance_id;
    cfg->name = name ? name : "GNSS_CFG";
    cfg->restore_state = NMEA_RESTORE_IDLE;
    strncpy(cfg->um980_port, UM980_PORT_COM2, sizeof(cfg->um980_port) - 1);

    /* Default conservative profile (Task #5) */
    nmea_profile_t stable = NMEA_PROFILE_STABLE;
    cfg->target_profile = stable;

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

void gnss_nmea_config_set_profile(gnss_nmea_config_t* cfg,
                                   const nmea_profile_t* profile)
{
    if (cfg == NULL || profile == NULL) return;
    cfg->target_profile = *profile;
}

void gnss_nmea_config_set_um980_port(gnss_nmea_config_t* cfg,
                                       const char* port)
{
    if (cfg == NULL || port == NULL) return;
    strncpy(cfg->um980_port, port, sizeof(cfg->um980_port) - 1);
    cfg->um980_port[sizeof(cfg->um980_port) - 1] = '\0';
}

void gnss_nmea_config_start_restore(gnss_nmea_config_t* cfg)
{
    if (cfg == NULL) return;

    /* Reset all state */
    cfg->restore_ok_count = 0;
    cfg->restore_fail_count = 0;
    cfg->restore_fail_cmd[0] = '\0';
    cfg->effect_flags = 0;
    cfg->verify_gsv_hz = 0;
    cfg->verify_gga_hz = 0;
    cfg->verify_rmc_hz = 0;
    cfg->verify_gsa_hz = 0;
    cfg->verify_gst_hz = 0;
    cfg->gsv_was_disabled = false;
    cfg->gst_is_available = false;

    /* Snapshot UART overruns at start */
    if (cfg->uart != NULL) {
        const transport_uart_stats_t* stats = transport_uart_get_stats(cfg->uart);
        if (stats != NULL) {
            cfg->verify_overruns_start = stats->rx_overflow_count;
        }
    }

    build_restore_queue(cfg);
    cfg->restore_state = NMEA_RESTORE_UNLOGGING;
    cfg->restore_start_us = now_us();

    /* Intercept UART RX buffer */
    if (cfg->gnss != NULL) {
        cfg->gnss->nmea_config_intercept = true;
    }

    ESP_LOGI(TAG, "%s: NMEA restore started (receiver %u, %u commands)",
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

uint32_t gnss_nmea_config_get_effect_flags(const gnss_nmea_config_t* cfg)
{
    if (cfg == NULL) return 0;
    return cfg->effect_flags;
}

/* =================================================================
 * Service Step — Restore State Machine
 *
 * States: IDLE → UNLOGGING → SETTING → VERIFYING → DONE/FAILED → IDLE
 *
 * During UNLOGGING/SETTING:
 *   - Resets result before each command
 *   - Sends command via UART TX
 *   - Reads response from UART RX (forwards NMEA to gnss_um980)
 *   - Waits for ACK or timeout
 *   - process_cmd_result() is the SINGLE exit point (cmd_index++)
 *
 * During VERIFYING:
 *   - Observes NMEA rates for VERIFY_WINDOW_MS
 *   - Classifies restore effect
 *   - Reports DONE or FAILED
 * ================================================================= */

void gnss_nmea_config_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    gnss_nmea_config_t* cfg = (gnss_nmea_config_t*)comp;
    if (cfg == NULL) return;

    switch (cfg->restore_state) {

    case NMEA_RESTORE_IDLE:
        return;

    case NMEA_RESTORE_UNLOGGING:
    case NMEA_RESTORE_SETTING:
    {
        /* Check if all commands completed */
        if (cfg->cmd_index >= cfg->cmd_count) {
            /* Transition to VERIFYING state (Task #4) */
            cfg->restore_state = NMEA_RESTORE_VERIFYING;
            cfg->verify_start_us = now_us();

            ESP_LOGI(TAG, "%s: commands done (ok=%u fail=%u), starting effect verification...",
                     cfg->name, cfg->restore_ok_count, cfg->restore_fail_count);
            break;
        }

        /* Process current command based on its status */
        switch (cfg->last_result.status) {

        case UM980_CMD_IDLE:
        {
            /* New command — send it */
            /* Enforce inter-command delay */
            if (cfg->last_cmd_us > 0) {
                uint64_t since_last = (timestamp_us - cfg->last_cmd_us) / 1000;
                if (since_last < UM980_INTER_CMD_DELAY_MS) {
                    return; /* wait */
                }
            }

            bool completed = send_current_cmd(cfg);
            if (completed) {
                /* TRANSPORT_FAIL — immediately finalize */
                process_cmd_result(cfg, timestamp_us);
            }
            /* else: sent, waiting for response */
            break;
        }

        case UM980_CMD_SENT:
        {
            /* Read response from UART RX */
            read_response(cfg);

            /* Check for timeout */
            uint64_t elapsed_us = timestamp_us - cfg->cmd_sent_us;
            bool timed_out = elapsed_us > (uint64_t)UM980_RESP_TIMEOUT_MS * 1000;

            if (timed_out) {
                cfg->last_result.status = UM980_CMD_TIMEOUT;
                process_cmd_result(cfg, timestamp_us);
            } else {
                /* Try early ACK detection */
                bool definitive = um980_parse_response(
                    cfg->response_buf, cfg->response_pos, &cfg->last_result);

                if (definitive && (cfg->last_result.ack_ok || cfg->last_result.ack_error)) {
                    cfg->last_result.status = cfg->last_result.ack_ok ?
                        UM980_CMD_OK : UM980_CMD_ERROR;
                    process_cmd_result(cfg, timestamp_us);
                }
                /* else: still waiting */
            }
            break;
        }

        case UM980_CMD_DONE:
        default:
            /* Previous command finalized — reset for next */
            reset_cmd_result(cfg);
            break;
        }

        /* Transition from UNLOGGING to SETTING */
        /* UNLOG commands: indices 0-8 (9 commands), LOG commands start at index 9 */
        if (cfg->restore_state == NMEA_RESTORE_UNLOGGING && cfg->cmd_index >= 9) {
            cfg->restore_state = NMEA_RESTORE_SETTING;
            ESP_LOGI(TAG, "%s: UNLOG phase complete, starting LOG phase", cfg->name);
        }
        break;
    }

    case NMEA_RESTORE_VERIFYING:
    {
        /* Wait for verification window */
        uint64_t verify_elapsed_ms = (now_us() - cfg->verify_start_us) / 1000;
        if (verify_elapsed_ms < UM980_VERIFY_WINDOW_MS) {
            /* Still observing — keep forwarding UART data to gnss */
            if (cfg->uart != NULL && cfg->gnss != NULL) {
                uint8_t tmp[128];
                size_t available = transport_uart_rx_available(cfg->uart);
                if (available > 0) {
                    size_t to_read = available > sizeof(tmp) ? sizeof(tmp) : available;
                    size_t pulled = transport_uart_rx_read(cfg->uart, tmp, to_read);
                    if (pulled > 0) {
                        gnss_um980_feed(cfg->gnss, tmp, pulled);
                        cfg->gnss->last_sentence_us = now_us();
                    }
                }
            }
            return;
        }

        /* Verification window complete — evaluate effect */
        evaluate_effect(cfg);

        /* Disconnect intercept */
        if (cfg->gnss != NULL) {
            cfg->gnss->nmea_config_intercept = false;
        }

        /* R3 B1/B2: Extended blocking flags — 0 Hz critical rates are FAIL */
        bool has_critical = (cfg->effect_flags &
            (RESTORE_EFFECT_GSV_STILL_ACTIVE | RESTORE_EFFECT_UART_OVERRUN |
             RESTORE_EFFECT_RING_FULL | RESTORE_EFFECT_NO_NMEA_OUTPUT |
             RESTORE_EFFECT_GGA_MISSING | RESTORE_EFFECT_RMC_MISSING)) != 0;

        /* R3 B2: GSA_HIGH is WARN but not blocking */
        bool has_warn = (cfg->effect_flags &
            (RESTORE_EFFECT_GSA_HIGH | RESTORE_EFFECT_GSA_MISSING)) != 0;

        if (has_critical) {
            cfg->restore_state = NMEA_RESTORE_FAILED;
            char flag_text[256];
            ESP_LOGE(TAG, "%s: === NMEA restore FAILED (effect verification) === "
                     "flags=0x%04x [%s] gsv=%.1f gga=%.1f rmc=%.1f",
                     cfg->name, cfg->effect_flags,
                     effect_flags_text(cfg->effect_flags, flag_text, sizeof(flag_text)),
                     cfg->verify_gsv_hz, cfg->verify_gga_hz, cfg->verify_rmc_hz);
        } else {
            /* R3 B2: Distinguish VERIFIED from VERIFIED_WITH_WARNINGS */
            if (has_warn) {
                char flag_text[256];
                ESP_LOGW(TAG, "%s: === NMEA restore OK (verified with warnings) === "
                         "gga=%.1f rmc=%.1f gst=%s gsa=%.1f gsv=%.1f overruns_delta=%u "
                         "flags=[%s]",
                         cfg->name,
                         cfg->verify_gga_hz, cfg->verify_rmc_hz,
                         cfg->gst_is_available ? "available" : "N/A",
                         cfg->verify_gsa_hz, cfg->verify_gsv_hz,
                         (unsigned)(cfg->verify_overruns_end - cfg->verify_overruns_start),
                         effect_flags_text(cfg->effect_flags, flag_text, sizeof(flag_text)));
            } else {
                ESP_LOGI(TAG, "%s: === NMEA restore OK (verified) === "
                         "gga=%.1f rmc=%.1f gst=%s gsa=%.1f gsv=%.1f overruns_delta=%u",
                         cfg->name,
                         cfg->verify_gga_hz, cfg->verify_rmc_hz,
                         cfg->gst_is_available ? "available" : "N/A",
                         cfg->verify_gsa_hz, cfg->verify_gsv_hz,
                         (unsigned)(cfg->verify_overruns_end - cfg->verify_overruns_start));
            }

            /* R4 FIX (Aufgabe 4): Transition to DONE — previously missing!
             * Without this, state stayed VERIFYING and completion was
             * logged repeatedly every service_step cycle. */
            cfg->restore_state = NMEA_RESTORE_DONE;
        }

        uint64_t total_ms = (now_us() - cfg->restore_start_us) / 1000;
        ESP_LOGI(TAG, "%s: restore completed in %llums (ok=%u fail=%u)",
                 cfg->name, (unsigned long long)total_ms,
                 cfg->restore_ok_count, cfg->restore_fail_count);
        return;
    }

    case NMEA_RESTORE_DONE:
    case NMEA_RESTORE_FAILED:
        return;
    }
}
