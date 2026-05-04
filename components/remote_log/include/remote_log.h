#pragma once

/* ========================================================================
 * remote_log.h — In-memory ringbuffer for ESP_LOG output
 *
 * NAV-REMOTE-LOG-001:
 *   - Intercepts ALL ESP_LOG output via esp_log_set_vprintf()
 *   - Stores in a statically allocated ringbuffer
 *   - Accessible via HTTP endpoints (/logs, /logs?tail=N, /logs/status)
 *
 * NAV-REMOTELOG-BOOTBUFFER-001:
 *   - Two-phase init: early_init() before scheduler, init() after
 *   - Boot logs captured before Ethernet/NVS/UART subsystem init
 *   - Early phase uses critical sections (no FreeRTOS primitives needed)
 *   - Late phase upgrades to mutex for ISR-safe multi-task access
 *   - No API changes to existing read/clear/stats functions
 *   - Extended stats: boot_lines_captured, early_init_called
 *
 * HARD RULES:
 *   - vprintf hook NEVER calls ESP_LOG*() (infinite recursion)
 *   - vprintf hook NEVER does malloc/network/blocking ops
 *   - vprintf hook uses try-lock / critical section only
 *   - Core-1 Fast Loop must NEVER be blocked by logging
 *   - Buffer is purely circular — oldest data overwritten on overflow
 * ======================================================================== */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Boot-buffer configuration (compile-time) ---- */
#define REMOTE_LOG_BOOT_BUFFER_SIZE  16384   /* 16 KB for pre-scheduler logs */

/* ---- Extended statistics ---- */
typedef struct {
    uint32_t lines_written;
    uint32_t lines_dropped;
    uint32_t bytes_written;
    uint32_t bytes_overwritten;
    size_t buffer_size;
    size_t used_bytes;
    /* NAV-REMOTELOG-BOOTBUFFER-001 additions */
    bool     early_init_called;      /* true if early_init() was invoked */
    bool     fully_initialized;      /* true if init() (mutex) completed */
    uint32_t boot_lines_captured;    /* lines captured during early phase */
    uint32_t boot_bytes_captured;    /* bytes captured during early phase */
} remote_log_stats_t;

/* ---- Phase 1: Early Init (before FreeRTOS scheduler) ----
 * Safe to call before xTaskCreate, before any FreeRTOS primitives.
 * Uses critical sections instead of mutex.
 * Installs vprintf hook immediately — all subsequent ESP_LOG output
 * is captured into a statically allocated boot buffer.
 * Can be called multiple times safely (idempotent). */
void remote_log_early_init(void);

/* ---- Phase 2: Full Init (after scheduler, before subsystem init) ----
 * Creates mutex, transfers boot buffer into main ringbuffer,
 * upgrades locking from critical sections to mutex.
 * Existing API remains fully backward compatible. */
void remote_log_init(void);

bool remote_log_is_initialized(void);

/* ---- Read API (unchanged) ---- */
size_t remote_log_read(char* out, size_t out_size);
size_t remote_log_read_tail_lines(char* out, size_t out_size, uint32_t max_lines);

/* ---- Control API (unchanged) ---- */
void remote_log_clear(void);
void remote_log_get_stats(remote_log_stats_t* out);

#ifdef __cplusplus
}
#endif
