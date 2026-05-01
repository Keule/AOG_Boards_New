#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Public API ---- */

void app_core_init(void);
void app_core_start(void);

/* ---- NAV-DIAG: Pure diagnostic step function ----
 *
 * Platform-independent, testable, no ESP-IDF dependencies.
 * Performs: eth_link update → health collect → recovery evaluate.
 * Does NOT log — caller can check the snapshot and recovery status.
 *
 * Parameters:
 *   collector:   nav_health_collector_t* (void* to avoid nav_diagnostics.h
 *                dependency in this header; cast in .c)
 *   snapshot:    nav_health_snapshot_t* (filled with current health state)
 *   recovery:    nav_recovery_status_t* (filled with recovery recommendations)
 *   now_ms:      Current uptime in milliseconds
 *   eth_link_up: Current Ethernet link state (set by caller)
 *
 * Non-blocking guarantees:
 *   - No I/O, no reconnects, no UART/TCP/UDP payload
 *   - No sleeps or delays
 *   - O(1) execution time
 *
 * Returns: true if recovery actions are recommended. */
bool app_core_nav_diag_step(void* collector,
                             void* snapshot,
                             void* recovery,
                             uint64_t now_ms,
                             bool eth_link_up);

#ifdef __cplusplus
}
#endif
