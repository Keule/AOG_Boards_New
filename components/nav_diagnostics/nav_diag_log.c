/* nav_diag_log.c — Rate-limited Logging Implementation (NAV-DIAG-001 Teil 2)
 *
 * Platform-unabhängige Implementierung.
 * Auf ESP32 kann nav_diag_log_emit_callback() gesetzt werden, um
 * ESP_LOGI/W/E aufzurufen. Ohne Callback: noop (stubs für native tests).
 */

#include "nav_diag_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- Global log stats ---- */

static nav_diag_log_stats_t s_log_stats = {0, 0, 0};

/* ---- Emit callback (set via nav_diag_log_set_emit_callback) ---- */
static nav_diag_emit_fn_t s_emit_callback = NULL;

/* ---- Default interval lookup ---- */

static uint32_t default_interval_for_level(nav_diag_level_t level)
{
    switch (level) {
        case NAV_DIAG_LEVEL_DEBUG: return NAV_DIAG_DEFAULT_INTERVAL_DEBUG;
        case NAV_DIAG_LEVEL_INFO:  return NAV_DIAG_DEFAULT_INTERVAL_INFO;
        case NAV_DIAG_LEVEL_WARN:  return NAV_DIAG_DEFAULT_INTERVAL_WARN;
        case NAV_DIAG_LEVEL_ERROR: return NAV_DIAG_DEFAULT_INTERVAL_ERROR;
        default:                   return NAV_DIAG_DEFAULT_INTERVAL_INFO;
    }
}

/* ---- Init ---- */

void nav_diag_log_entry_init(nav_diag_log_entry_t* entry,
                              nav_diag_level_t level)
{
    if (entry == NULL) { return; }
    memset(entry, 0, sizeof(nav_diag_log_entry_t));
    entry->interval_ms = default_interval_for_level(level);
}

void nav_diag_log_entry_init_ex(nav_diag_log_entry_t* entry,
                                 uint32_t interval_ms)
{
    if (entry == NULL) { return; }
    memset(entry, 0, sizeof(nav_diag_log_entry_t));
    entry->interval_ms = interval_ms > 0 ? interval_ms : 1000;
}

void nav_diag_log_entry_reset(nav_diag_log_entry_t* entry)
{
    if (entry == NULL) { return; }
    entry->last_emit_ms = 0;
    entry->suppressed_count = 0;
    entry->total_count = 0;
    entry->has_pending = false;
}

/* ---- Core emit function ---- */

bool nav_diag_log_emit(nav_diag_log_entry_t* entry,
                        nav_diag_level_t level,
                        const char* module,
                        uint64_t timestamp_ms,
                        const char* fmt, ...)
{
    if (entry == NULL || module == NULL || fmt == NULL) {
        return false;
    }

    entry->total_count++;

    /* Rate-limit check: skip only for the very first call (total_count == 1)
     * so that a first emit at timestamp_ms == 0 is never suppressed.
     * Using total_count avoids the edge-case where last_emit_ms == 0. */
    if (entry->interval_ms > 0 && entry->total_count > 1) {
        if (timestamp_ms > 0 &&
            (timestamp_ms - entry->last_emit_ms) < (uint64_t)entry->interval_ms) {
            /* Suppress this message */
            entry->suppressed_count++;
            entry->has_pending = true;
            s_log_stats.total_suppressed++;
            return false;
        }
    }

    /* Format message */
    char msg_buf[NAV_DIAG_MAX_MSG_LEN + 32]; /* extra space for level + module prefix */
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    if (written < 0) {
        written = 0;
    }
    if ((size_t)written >= sizeof(msg_buf)) {
        msg_buf[sizeof(msg_buf) - 1] = '\0';
    }

    /* Build full output line: [LEVEL] MODULE: message [+N suppressed] */
    char full_line[NAV_DIAG_MAX_MSG_LEN + 64];
    const char* level_str = nav_diag_level_name(level);

    if (entry->has_pending && entry->suppressed_count > 0) {
        snprintf(full_line, sizeof(full_line), "[%s] %s: %s (+%u suppressed)",
                 level_str, module, msg_buf, (unsigned)entry->suppressed_count);
        entry->suppressed_count = 0;
        entry->has_pending = false;
    } else {
        snprintf(full_line, sizeof(full_line), "[%s] %s: %s",
                 level_str, module, msg_buf);
    }

    /* Update timing */
    entry->last_emit_ms = timestamp_ms;
    s_log_stats.total_emitted++;
    if (level == NAV_DIAG_LEVEL_ERROR) {
        s_log_stats.total_errors++;
    }

    /* Output via callback or stub */
    if (s_emit_callback != NULL) {
        s_emit_callback(level, module, full_line);
    }
    /* else: silent on native/no-OS platforms */

    return true;
}

/* ---- Error counter helper ---- */

uint32_t nav_diag_error_increment(uint32_t* counter, const char* module,
                                   const char* description)
{
    if (counter == NULL) { return 0; }
    (*counter)++;
    (void)module;
    (void)description;
    return *counter;
}

/* ---- Global stats ---- */

const nav_diag_log_stats_t* nav_diag_log_get_stats(void)
{
    return &s_log_stats;
}

void nav_diag_log_stats_reset(void)
{
    memset(&s_log_stats, 0, sizeof(s_log_stats));
}

/* ---- Level name ---- */

const char* nav_diag_level_name(nav_diag_level_t level)
{
    switch (level) {
        case NAV_DIAG_LEVEL_DEBUG: return "DBG";
        case NAV_DIAG_LEVEL_INFO:  return "INF";
        case NAV_DIAG_LEVEL_WARN:  return "WRN";
        case NAV_DIAG_LEVEL_ERROR: return "ERR";
        default:                   return "???";
    }
}

/* ---- Emit callback setter ---- */

void nav_diag_log_set_emit_callback(nav_diag_emit_fn_t cb)
{
    s_emit_callback = cb;
}
