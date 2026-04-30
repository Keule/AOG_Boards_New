#pragma once

/* ========================================================================
 * aog_navigation_app.h — AOG/AgIO GPS & Heading Output (NAV-AOG-001)
 *
 * Pure business logic component. Prepares AOG output frames (PGN 214).
 *
 * DATA FLOW:
 *   primary gnss_snapshot (via snapshot_buffer) ──┐
 *                                               ├── aog_navigation_app
 *   heading snapshot (via snapshot_buffer)      ──┘    ──→ AOG TX Frame Queue
 *
 * OUTPUT PGN:
 *   PGN 214 — Combined GPS + Heading + Speed + Altitude
 *   Replaces separate PGN 200 (position) + PGN 201 (heading).
 *
 * INPUTS (references set by caller, NOT owned):
 *   - Primary GNSS position snapshot (via snapshot_buffer_t*)
 *   - Heading snapshot (via snapshot_buffer_t*)
 *   - AOG RX frames (via byte_ring_buffer_t*) — for Hello detection
 *
 * OUTPUTS:
 *   - AOG TX frames (via byte_ring_buffer_t*) — PGN 214 + Hello Response
 *
 * This component does NOT:
 *   - own GNSS receivers (no gnss_um980_t*)
 *   - call transport functions (no UDP, UART, TCP)
 *   - call HAL functions
 *   - send Hello cyclically (only on request)
 *   - operate NTRIP/RTCM state machines
 *
 * HARD RULES:
 *   - No direct physical network send (layer separation)
 *   - No UART I/O
 *   - No steering changes
 *   - PGN encoding stays in protocol_aog
 *   - Transport stays transport-only
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "runtime_component.h"
#include "aog_pgn.h"
#include "aog_frame.h"
#include "byte_ring_buffer.h"
#include "snapshot_buffer.h"
#include "nmea_parser.h"
#include "gnss_dual_heading.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- AOG Navigation App Instance ---- */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    /* Input references (NOT owned, set by caller) */
    const snapshot_buffer_t* position_source;    /* primary GNSS GGA snapshot buffer */
    const snapshot_buffer_t* heading_source;     /* heading snapshot buffer */
    byte_ring_buffer_t*    aog_rx_source;       /* reads incoming AOG frames */
    byte_ring_buffer_t*    aog_tx_dest;         /* writes outgoing AOG frames */

    /* ---- State flags ---- */
    bool               position_valid;
    bool               heading_valid;

    /* ---- AOG send timing ---- */
    uint32_t           last_aog_send_ms;
    uint32_t           aog_send_interval_ms;  /* e.g. 50ms = 20 Hz */

    /* ---- Incoming AOG frame parser (for Hello/Discovery detection) ---- */
    aog_parser_t       aog_rx_parser;

    /* Hello response pending flag */
    bool               hello_response_pending;

    /* Stats */
    uint32_t           cycle_count;
    uint32_t           pgn214_sent_count;
} aog_nav_app_t;

/* ---- API ---- */

/* Initialize navigation application. */
void aog_nav_app_init(aog_nav_app_t* app);

/* Set input references (NOT owned). Called during wiring by app_core. */
void aog_nav_app_set_position_source(aog_nav_app_t* app, const snapshot_buffer_t* source);
void aog_nav_app_set_heading_source(aog_nav_app_t* app, const snapshot_buffer_t* source);
void aog_nav_app_set_aog_rx_source(aog_nav_app_t* app, byte_ring_buffer_t* source);
void aog_nav_app_set_aog_tx_dest(aog_nav_app_t* app, byte_ring_buffer_t* dest);

/* Service step: read inputs, prepare AOG PGN 214 output frame.
 * Called by the runtime service loop. */
void aog_nav_app_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* Get the runtime_component pointer (for registration). */
runtime_component_t* aog_nav_app_get_component(aog_nav_app_t* app);

#ifdef __cplusplus
}
#endif
