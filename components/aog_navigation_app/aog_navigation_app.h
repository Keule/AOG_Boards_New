#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "runtime_component.h"
#include "aog_pgn.h"
#include "aog_frame.h"
#include "byte_ring_buffer.h"
#include "snapshot_buffer.h"
#include "gnss_snapshot.h"
#include "gnss_dual_heading.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- AOG Navigation App Instance ----
 *
 * Pure business logic component. Prepares AOG PGN 214 output frames
 * from GNSS and Heading snapshots. Implements strict output gating
 * and stale-data suppression per NAV-AOG-001 Nacharbeit.
 *
 * Inputs (references set by caller, NOT owned):
 *   - Primary GNSS position snapshot (gnss_snapshot_t via snapshot_buffer_t*)
 *   - Heading snapshot (gnss_dual_heading_t via snapshot_buffer_t*)
 *   - AOG RX frames (via byte_ring_buffer_t*)
 *
 * Outputs:
 *   - AOG TX frames via byte_ring_buffer_t* (PGN 214, Hello, Scan Reply)
 *
 * OUTPUT GATING RULES:
 *   GNSS output suppressed when:
 *     - GNSS snapshot invalid (valid=false)
 *     - GNSS snapshot stale (fresh=false)
 *   Heading sent as sentinel when:
 *     - Heading snapshot invalid (valid=false)
 *     - Heading snapshot stale (no update within timeout)
 *
 * This component does NOT:
 *   - own GNSS receivers
 *   - call transport functions (no UDP, UART, TCP)
 *   - call HAL functions
 *   - modify GNSS or Heading components
 *   - operate NTRIP/RTCM state machines
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting. */

/* ---- Output Gating State (NAV-AOG-001 FINAL HARDENING) ----
 *
 * 8-state model covering all GNSS + Heading combinations.
 * Maps to AOG fix_quality byte and diagnostic output.
 *
 * State                  | fix_quality | Heading | Description
 * ------------------------|-------------|---------|----------------------------
 * OK                      | from GGA    | real    | Full live frame
 * GNSS_INVALID            | 0 (NONE)    | sentin  | No GNSS fix at all
 * GNSS_STALE              | 0 (NONE)    | sentin  | GNSS was valid, now expired
 * HEADING_INVALID         | from GGA    | sentin  | GNSS OK, heading source failure
 * HEADING_STALE           | from GGA    | sentin  | GNSS OK, heading expired
 * HEADING_LOST            | from GGA    | sentin  | Heading source disappeared
 * SUPPRESSED              | N/A         | N/A     | Output entirely suppressed
 * INIT                     | N/A         | N/A     | Before first service step
 *
 * ---- Heading 3-State Model (FINAL HARDENING) ----
 *
 * VALID:   heading_dual = real degrees (0-360), heading_output_active = true
 *          Condition: source present AND valid=true AND fresh (< freshness_ms)
 *          Action:    Normal heading output in PGN 214
 *
 * STALE:   heading_dual = FLT_MAX sentinel, heading_output_active = false
 *          Condition: source present AND valid=true BUT stale (>= freshness_ms)
 *          Action:    Suppress heading, send sentinel, keep GNSS alive
 *          PROHIBITED: last known value, silent fallback, zero heading
 *
 * INVALID: heading_dual = FLT_MAX sentinel, heading_output_active = false
 *          Sub-cases:
 *            - HEADING_INVALID: source present, valid=false
 *            - HEADING_LOST: source disappeared entirely (NULL)
 *          Action:    Suppress heading, send sentinel, keep GNSS alive
 *          PROHIBITED: last known value, silent fallback, zero heading
 *
 * ---- Status/Fix Mapping (FINAL HARDENING) ----
 *
 * Internal State   | fix_quality byte | heading_dual    | Notes
 * -----------------|-------------------|-----------------|--------------------
 * OK               | from GGA (1/2/4/5)| real (deg)      | Full live data
 * GNSS_INVALID     | 0 (NONE)          | FLT_MAX sentin  | No fix at all
 * GNSS_STALE       | 0 (NONE)          | FLT_MAX sentin  | Fix expired
 * HEADING_INVALID  | from GGA (1/2/4/5)| FLT_MAX sentin  | Heading failure
 * HEADING_STALE    | from GGA (1/2/4/5)| FLT_MAX sentin  | Heading expired
 * HEADING_LOST     | from GGA (1/2/4/5)| FLT_MAX sentin  | Source gone
 * SUPPRESSED       | N/A               | N/A             | All output off
 * INIT             | N/A               | N/A             | Pre-first-step
 *
 * Fix Quality Byte Values (in PGN 214 payload byte 38):
 *   0 = NONE (no fix, stale, invalid)
 *   1 = GPS  (autonomous)
 *   2 = DGPS (differential)
 *   4 = RTK FIX
 *   5 = RTK FLOAT
 *   Values 3, 6+ → mapped to 0 (NONE)
 */
typedef enum {
    AOG_OUTPUT_INIT = 0,         /* Before first service step */
    AOG_OUTPUT_OK,               /* GNSS valid+fresh, heading valid+fresh */
    AOG_OUTPUT_GNSS_INVALID,     /* GNSS not valid (no fix) */
    AOG_OUTPUT_GNSS_STALE,       /* GNSS valid but stale (> freshness threshold) */
    AOG_OUTPUT_HEADING_INVALID,  /* GNSS OK, heading source reports invalid */
    AOG_OUTPUT_HEADING_STALE,    /* GNSS OK, heading valid but stale (> freshness) */
    AOG_OUTPUT_HEADING_LOST,     /* GNSS OK, heading source disappeared entirely */
    AOG_OUTPUT_SUPPRESSED        /* All output suppressed */
} aog_output_state_t;

/* ---- Heading freshness configuration ---- */
#define AOG_HEADING_FRESHNESS_MS_DEFAULT  3000  /* 3 seconds */
#define AOG_HEADING_FRESHNESS_MS_MIN      500   /* 500ms floor */
#define AOG_HEADING_FRESHNESS_MS_MAX      10000 /* 10s cap */

typedef struct {
    runtime_component_t component;    /* MUST be first */

    /* Input references (NOT owned, set by caller) */
    const snapshot_buffer_t* position_source;    /* primary GNSS snapshot buffer (gnss_snapshot_t) */
    const snapshot_buffer_t* heading_source;     /* heading snapshot buffer (gnss_dual_heading_t) */
    byte_ring_buffer_t*    aog_rx_source;      /* reads incoming AOG frames */
    byte_ring_buffer_t*    aog_tx_dest;        /* writes outgoing AOG frames */

    /* Source address for outgoing PGN frames */
    uint8_t            src_address;           /* e.g., AOG_SRC_GPS (0x05) */

    /* Output state tracking */
    aog_output_state_t output_state;
    bool               gnss_output_active;   /* true: last cycle had valid GNSS */
    bool               heading_output_active; /* true: last cycle had valid heading */
    uint32_t           suppress_count;        /* number of suppressed cycles */

    /* AOG send timing */
    uint32_t           last_aog_send_ms;
    uint32_t           aog_send_interval_ms;  /* e.g. 50ms = 20 Hz */

    /* Heading freshness tracking */
    uint32_t           heading_freshness_ms;
    uint32_t           last_heading_update_ms;   /* timestamp of last heading sequence change */
    uint32_t           last_heading_sequence;    /* snapshot sequence of last seen heading */
    bool               heading_ever_seen;       /* true if at least one valid heading was seen */

    /* Module type for Discovery (PGN-Verzeichnis: GPS = 120/0x78) */
    uint8_t            module_type;

    /* Incoming AOG frame parser (tolerant CRC for Discovery PGNs) */
    aog_parser_t       aog_rx_parser;

    /* Pending response flags */
    bool               hello_response_pending;
    bool               scan_response_pending;

    /* Stats */
    uint32_t           cycle_count;
    uint32_t           pgn214_send_count;
    uint32_t           hello_send_count;
    uint32_t           scan_send_count;
} aog_nav_app_t;

/* ---- API ---- */

/* Initialize navigation application with defaults.
 * src_address defaults to AOG_SRC_GPS (0x05).
 * send_interval defaults to 50ms (20 Hz).
 * heading_freshness defaults to 3000ms. */
void aog_nav_app_init(aog_nav_app_t* app);

/* Set input references (NOT owned). Called during wiring by app_core. */
void aog_nav_app_set_position_source(aog_nav_app_t* app, const snapshot_buffer_t* source);
void aog_nav_app_set_heading_source(aog_nav_app_t* app, const snapshot_buffer_t* source);
void aog_nav_app_set_aog_rx_source(aog_nav_app_t* app, byte_ring_buffer_t* source);
void aog_nav_app_set_aog_tx_dest(aog_nav_app_t* app, byte_ring_buffer_t* dest);

/* Set source address for outgoing PGN frames (default: AOG_SRC_GPS). */
void aog_nav_app_set_src_address(aog_nav_app_t* app, uint8_t src);

/* Set heading freshness timeout in ms (clamped). */
void aog_nav_app_set_heading_freshness(aog_nav_app_t* app, uint32_t timeout_ms);

/* Service step: read inputs, prepare AOG PGN 214 output frames.
 * Called by the runtime service loop. */
void aog_nav_app_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* Get the runtime_component pointer (for registration). */
runtime_component_t* aog_nav_app_get_component(aog_nav_app_t* app);

/* Get current output state (for diagnostics). */
aog_output_state_t aog_nav_app_get_output_state(const aog_nav_app_t* app);

#ifdef __cplusplus
}
#endif
