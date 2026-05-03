#pragma once
/* ========================================================================
 * hw_runtime_diag.h — Hardware Runtime Diagnostics (NAV-HW-RUNTIME-DIAG-001)
 *
 * Lightweight diagnostic component that prints periodic health summaries
 * for the fast loop, GNSS UARTs, GNSS snapshots, Ethernet/UDP, NTRIP,
 * and PGN214 TX at 0.2 Hz (every 5 seconds).
 *
 * HARD RULES:
 *   - No changes to GNSS/PGN214/NTRIP/RTCM/Steering logic
 *   - No logging in the 100 Hz fast path
 *   - No blocking operations
 *   - READ-ONLY access to all observed components
 *   - No NTRIP secrets (passwords, tokens) in log output
 *
 * Output format (example):
 *   RUNTIME_FAST: hz=99.8 report_cycles=500 total_cycles=2534 ...
 *   GNSS_RX: primary uart=1 bytes=1234 lines=56 checksum_ok=54 checksum_bad=2
 *   GNSS_RX: secondary uart=2 bytes=0 lines=0 checksum_ok=0 checksum_bad=0
 *   GNSS: primary valid=1 fresh=1 fix=rtk age_ms=120
 *   GNSS: secondary valid=0 fresh=0 fix=none age_ms=0
 *   HEADING: valid=0 heading_deg=0.0 calc_count=0
 *   ETH: driver=RTL8201/RMII link=up ip=192.168.1.59
 *   UDP: ready rx=ok tx=ok local_port=9999 target=255.255.255.255:9999 peer_seen=0
 *   NTRIP: eth_ready=1 config_ready=0 state=disabled reason=empty_config
 *   NTRIP_CFG: host=empty port=0 mount=empty user=empty password=empty
 *   AOG_NAV: pgn214_tx=500 cycle=500 state=OK
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include "runtime_component.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Health print interval in milliseconds (5 seconds = 0.2 Hz) */
#define HW_DIAG_PRINT_INTERVAL_MS  5000

/* Diagnostic context: references to all observed components (NOT owned) */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    /* Tick counter for 5s interval */
    uint64_t last_print_ms;
    bool     first_print_done;

    /* ---- References (set by caller, NOT owned) ---- */
    /* These are void* to avoid circular includes.
     * The .c file includes the actual headers for type-safe access. */
    const void* primary_uart;      /* transport_uart_t* */
    const void* secondary_uart;    /* transport_uart_t* */
    const void* primary_gnss;      /* gnss_um980_t* */
    const void* secondary_gnss;    /* gnss_um980_t* */
    const void* heading;           /* gnss_dual_heading_calc_t* */
    const void* nav_app;           /* aog_nav_app_t* */
    const void* udp;               /* transport_udp_t*   (NAV-ETH-BRINGUP-001-R2 WP-B) */
    const void* ntrip;             /* ntrip_client_t*    (NAV-ETH-BRINGUP-001-R2 WP-H) */
} hw_runtime_diag_t;

/* Initialize diagnostic component.
 * Does NOT start printing — call hw_runtime_diag_set_sources() first. */
void hw_runtime_diag_init(hw_runtime_diag_t* diag);

/* Set observed component references (NOT owned).
 * Call after init, before runtime_start(). */
void hw_runtime_diag_set_sources(hw_runtime_diag_t* diag,
                                  const void* primary_uart,
                                  const void* secondary_uart,
                                  const void* primary_gnss,
                                  const void* secondary_gnss,
                                  const void* heading,
                                  const void* nav_app,
                                  const void* udp,
                                  const void* ntrip);

/* Service step: called from DIAG service group (Core 0, ~100ms period).
 * Prints health summary every 5 seconds. */
void hw_runtime_diag_service_step(runtime_component_t* comp, uint64_t timestamp_us);

#ifdef __cplusplus
}
#endif
