#pragma once
/* ========================================================================
 * remote_diag.h — Remote Diagnostics over Ethernet (NAV-REMOTE-DIAG-001)
 *
 * Minimal HTTP server providing /status, /diag, /version endpoints.
 * Runs exclusively on Core 0 (SERVICE_GROUP_DIAGNOSTICS).
 *
 * HARD RULES:
 *   - No code in task_fast / Core 1
 *   - HTTP server starts only after Ethernet got_ip
 *   - Missing values reported as "not_available" — endpoint never fails
 *   - No large web UI, no full CLI
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include "runtime_component.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- HTTP server configuration ---- */
#define REMOTE_DIAG_HTTP_PORT       80
#define REMOTE_DIAG_MAX_URI_HANDLERS 4

/* ---- Remote Diag Instance ---- */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    /* ---- Observed component references (set by app_core, NOT owned) ---- */
    const void* primary_uart;         /* transport_uart_t* */
    const void* secondary_uart;       /* transport_uart_t* */
    const void* primary_gnss;         /* gnss_um980_t* */
    const void* secondary_gnss;       /* gnss_um980_t* */
    const void* heading;              /* gnss_dual_heading_calc_t* */
    const void* nav_app;              /* aog_nav_app_t* */
    const void* udp;                  /* transport_udp_t* */
    const void* ntrip;                /* ntrip_client_t* */
    const void* rtcm_router;          /* rtcm_router_t* */

    /* ---- Internal state ---- */
    bool     http_started;
    uint64_t boot_time_us;            /* esp_timer_get_time() at first service_step */
} remote_diag_t;

/* ---- API ---- */

/* Initialize remote diagnostics component.
 * Sets up component metadata, all references to NULL. */
void remote_diag_init(remote_diag_t* diag);

/* Set observed component references (called by app_core during wiring).
 * All pointers are stored as const void* and cast internally.
 * NULL pointers are safe — the endpoint will report "not_available". */
void remote_diag_set_sources(remote_diag_t* diag,
                              const void* primary_uart,
                              const void* secondary_uart,
                              const void* primary_gnss,
                              const void* secondary_gnss,
                              const void* heading,
                              const void* nav_app,
                              const void* udp,
                              const void* ntrip,
                              const void* rtcm_router);

/* Service step: start/stop HTTP server based on Ethernet state,
 * handle periodic housekeeping.
 * Called by the runtime service loop (Core 0). */
void remote_diag_service_step(runtime_component_t* comp, uint64_t timestamp_us);

#ifdef __cplusplus
}
#endif
