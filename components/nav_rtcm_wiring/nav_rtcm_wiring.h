#pragma once

#include <stdbool.h>
#include "rtcm_router.h"
#include "byte_ring_buffer.h"
#include "board_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Maximum supported RTCM targets ----
 * Currently 2 (Primary + Secondary).  If board profiles grow to 3+
 * receivers, this constant and RTCM_ROUTER_MAX_OUTPUTS must be raised
 * in sync.  nav_rtcm_wire_outputs() will detect the capacity mismatch
 * and report a clear error. */
#define NAV_RTCM_MAX_TARGETS  4

/* ---- RTCM Wiring Target Descriptor ---- */

typedef struct {
    board_uart_port_t   port;       /* GNSS UART port enum */
    byte_ring_buffer_t* tx_buffer;  /* UART TX buffer (NOT owned) — NULL if unavailable */
} nav_rtcm_target_t;

/* ---- RTCM Wiring Result ---- */

typedef struct {
    bool        productive;              /* true if all targets wired successfully */
    int         active_target_count;     /* number of active GNSS receivers in profile */
    int         registered_output_count; /* number of router outputs registered */
    const char* error_detail;            /* NULL if productive; error message otherwise */
} nav_rtcm_wiring_result_t;

/* ---- Wire RTCM outputs for all active GNSS receivers (pure function) ----
 *
 * Iterates over the provided target list and registers each active GNSS
 * receiver's TX buffer as an RTCM router output.  The function is pure
 * (no I/O, no globals, no side effects other than router state).
 *
 * For each target:
 *   1. Skip if board_profile_has_uart(port) == false  (not in this profile)
 *   2. FAIL if board_profile_has_uart_tx(port) == false (active but no TX pin)
 *   3. FAIL if tx_buffer == NULL (no transport buffer wired)
 *   4. FAIL if rtcm_router_add_output() < 0 (router at capacity)
 *   5. Otherwise: register output, increment counters
 *
 * After iterating all targets:
 *   - productive = (registered_output_count == active_target_count)
 *   - If no active targets found: productive = false, error_detail set
 *
 * Returns: nav_rtcm_wiring_result_t with full diagnostics.
 *
 * Thread safety: NOT thread-safe.  Must be called from init context only. */
nav_rtcm_wiring_result_t nav_rtcm_wire_outputs(
    const nav_rtcm_target_t* targets,
    int target_count,
    rtcm_router_t* router
);

#ifdef __cplusplus
}
#endif
