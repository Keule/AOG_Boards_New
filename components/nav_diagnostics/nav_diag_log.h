#pragma once
/* ========================================================================
 * nav_diag_log.h — Rate-limitiertes Logging (NAV-DIAG-001 Teil 2)
 *
 * Bietet ein Logging-Makro/Utility, das:
 *   - Rate-limitiert ist (verhindert High-Frequency-Spam)
 *   - Modulnamen enthält
 *   - Fehlerzähler automatisch inkrementiert
 *   - Ohne ESP_LOGI/E-Abhängigkeit funktioniert (platform-unabhängig)
 *
 * Konzept:
 *   - nav_diag_log_entry_t: Ein einzelner Log-Filter-Eintrag
 *   - NAV_DIAG_LOG(): Makro für rate-limitiertes Logging
 *   - nav_diag_log_emit(): Eigentliche Ausgabe (kann mit ESP_LOGI/W/E
 *     überlagert werden oder auf serielle Konsole gehen)
 *
 * Jedes Modul deklariert ein statisches nav_diag_log_entry_t mit
 * Mindest-Intervall. Wenn zwei Logs mit gleichem Tag+Level+Message
 * innerhalb des Intervalls auftreten, wird das zweite unterdrückt
 * und nur der Zähler erhöht.
 *
 * Speicherbedarf: Pro Log-Eintrag ~20 Bytes (sizeof nav_diag_log_entry_t).
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Log Levels ---- */

typedef enum {
    NAV_DIAG_LEVEL_DEBUG = 0,
    NAV_DIAG_LEVEL_INFO,
    NAV_DIAG_LEVEL_WARN,
    NAV_DIAG_LEVEL_ERROR,
} nav_diag_level_t;

/* ---- Default intervals (milliseconds) ---- */

#define NAV_DIAG_DEFAULT_INTERVAL_DEBUG  5000    /* 5 seconds */
#define NAV_DIAG_DEFAULT_INTERVAL_INFO   2000    /* 2 seconds */
#define NAV_DIAG_DEFAULT_INTERVAL_WARN   1000    /* 1 second */
#define NAV_DIAG_DEFAULT_INTERVAL_ERROR  500     /* 500ms — errors faster */

/* ---- Maximum module name length ---- */
#define NAV_DIAG_MAX_MODULE_LEN  24

/* ---- Maximum log message length ---- */
#define NAV_DIAG_MAX_MSG_LEN     80

/* ---- Log Entry (one per unique log site) ---- */

typedef struct {
    uint32_t interval_ms;       /* minimum ms between two emissions */
    uint64_t last_emit_ms;      /* timestamp of last emission */
    uint32_t suppressed_count;  /* number of suppressed messages */
    uint32_t total_count;       /* total number of times this log was called */
    bool     has_pending;       /* true if there are suppressed messages */
} nav_diag_log_entry_t;

/* ---- Global stats (cumulative, across all modules) ---- */

typedef struct {
    uint32_t total_emitted;     /* total log lines actually emitted */
    uint32_t total_suppressed;  /* total log lines rate-limited away */
    uint32_t total_errors;      /* total ERROR-level emissions */
} nav_diag_log_stats_t;

/* ---- API ---- */

/* Initialize a log entry with default interval for the given level. */
void nav_diag_log_entry_init(nav_diag_log_entry_t* entry,
                              nav_diag_level_t level);

/* Initialize a log entry with a custom interval. */
void nav_diag_log_entry_init_ex(nav_diag_log_entry_t* entry,
                                 uint32_t interval_ms);

/* Reset a log entry (clear counters, allow immediate next emission). */
void nav_diag_log_entry_reset(nav_diag_log_entry_t* entry);

/* ---- Emit function (platform-bridging point) ----
 *
 * Default implementation: stub (no output).
 * On ESP32: can be overridden to call ESP_LOGI/W/E.
 *
 * The emit function receives the formatted message and should output it
 * to the appropriate console.
 *
 * Returns: true if message was actually emitted, false if suppressed. */
bool nav_diag_log_emit(nav_diag_log_entry_t* entry,
                        nav_diag_level_t level,
                        const char* module,
                        uint64_t timestamp_ms,
                        const char* fmt, ...);

/* ---- Get global stats ---- */
const nav_diag_log_stats_t* nav_diag_log_get_stats(void);

/* ---- Reset global stats ---- */
void nav_diag_log_stats_reset(void);

/* ---- Level name helper ---- */
const char* nav_diag_level_name(nav_diag_level_t level);

/* ---- Convenience macros ----
 *
 * Declare a rate-limited log entry at file scope:
 *   static nav_diag_log_entry_t s_log_warn = NAV_DIAG_LOG_ENTRY_INIT(WARN);
 *
 * Use in code:
 *   NAV_DIAG_LOG(&s_log_warn, NAV_DIAG_LEVEL_WARN, "NTRIP",
 *                "connection timeout, retrying");
 */

#define NAV_DIAG_LOG_ENTRY_INIT(level) \
    { NAV_DIAG_DEFAULT_INTERVAL_##level, 0, 0, 0, false }

#define NAV_DIAG_LOG(entry, lvl, mod, ...) \
    nav_diag_log_emit((entry), (lvl), (mod), 0, __VA_ARGS__)

/* ---- Error counter helper ----
 *
 * Atomically increments an error counter and optionally logs.
 * Returns the new counter value.
 * Usage:
 *   gnss_checksum_err += 1;  // in component code
 *   nav_diag_log_error_counter("GNSS1", &s_checksum_err,
 *                              "checksum error on sentence");
 */
uint32_t nav_diag_error_increment(uint32_t* counter, const char* module,
                                   const char* description);

/* ---- Set emit callback (for ESP32 ESP_LOGI/W/E integration) ---- */
typedef void (*nav_diag_emit_fn_t)(nav_diag_level_t level,
                                    const char* module,
                                    const char* message);
void nav_diag_log_set_emit_callback(nav_diag_emit_fn_t cb);

#ifdef __cplusplus
}
#endif
