#pragma once
/* ========================================================================
 * gnss_um980.h — UM980 GNSS Receiver Component (NAV-GNSS-VALID-001)
 *
 * Consumes raw NMEA bytes from an external RX source buffer.
 * Parses GGA, RMC, GST NMEA sentences and maintains a unified
 * GNSS snapshot with Variant A validity flags and freshness tracking.
 *
 * HARD RULES:
 *   - No direct UART I/O in this component
 *   - No PGN 214 output
 *   - No auto UM980 configuration
 *   - Buffer-based only (via feed API or rx_source)
 *
 * VALIDITY MODEL (Variant A):
 *   position_valid = fresh GGA with fix_quality > 0
 *   motion_valid   = fresh RMC with status A
 *   accuracy_valid = fresh GST received (optional)
 *   valid          = position_valid AND motion_valid
 *   fresh          = valid AND no staleness
 *
 * CONSUMER USAGE:
 *   NAV-HEADING-001 → needs position_valid + fresh
 *   NAV-AOG-001     → needs valid + fresh
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "runtime_component.h"
#include "nmea_parser.h"
#include "gnss_snapshot.h"
#include "byte_ring_buffer.h"
#include "snapshot_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- UM980 Receiver Instance ---- */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    /* Identity */
    uint8_t     instance_id;    /* 0 = primary, 1 = secondary */
    const char* name;

    /* NMEA streaming parser (per-instance, fully isolated) */
    nmea_parser_t nmea_parser;

    /* RX data source (set by caller, NOT owned).
     * Typically points to transport_uart.rx_buffer. */
    byte_ring_buffer_t* rx_source;

    /* ---- Latest parsed NMEA data (per sentence type) ---- */
    nmea_gga_t gga;
    nmea_rmc_t rmc;
    nmea_gst_t gst;
    bool       gga_valid;
    bool       rmc_valid;
    bool       gst_valid;

    /* ---- Unified GNSS snapshot (merged from GGA+RMC+GST) ---- */
    gnss_snapshot_t snapshot;

    /* ---- Position snapshot buffer (for downstream consumers) ----
     * Publishes the full gnss_snapshot_t atomically via snapshot_buffer.
     * Consumers (e.g., aog_navigation_app) read via snapshot_buffer_get(). */
    gnss_snapshot_t    position_storage;    /* backing storage */
    snapshot_buffer_t  position_snapshot;   /* atomic wrapper around storage */

    /* ---- Freshness configuration ---- */
    uint32_t freshness_timeout_ms;

    /* ---- Per-instance parser statistics ---- */
    uint32_t sentences_parsed;     /* total valid sentences */
    uint32_t checksum_errors;      /* NMEA checksum mismatches */
    uint32_t overflow_errors;      /* sentence too long */
    uint32_t binary_rejects;       /* non-printable byte in sentence (NAV-GNSS-NMEA-CORRUPTION-001) */
    uint32_t garbage_discarded;    /* non-'$' bytes in IDLE state (RTCM binary) */
    uint32_t timeout_events;       /* freshness timeout count */
    uint32_t bytes_received;       /* total bytes fed */

    /* ---- Per-sentence-type counters ---- */
    uint32_t gga_count;
    uint32_t rmc_count;
    uint32_t gst_count;
    uint32_t gsa_count;             /* R2: GSA sentence counter */
    uint32_t gsv_count;             /* R2: GSV sentence counter */
    uint32_t unknown_prefix_count;  /* Valid sentences with unrecognized prefix (NAV-GNSS-RUNTIME-FIX-001) */

    /* ---- Dirty flags (set in feed, cleared in rebuild) ---- */
    bool gga_dirty;
    bool rmc_dirty;
    bool gst_dirty;

    /* ---- Timing ---- */
    uint64_t last_service_ms;      /* last service_step timestamp (ms) */

    /* ---- R2: Last sentence tracking ---- */
    uint64_t last_sentence_us;      /* timestamp of last valid sentence (us) */
    uint32_t last_sentence_type;    /* nmea_sentence_type_t of last valid sentence */
} gnss_um980_t;

/* ---- API ---- */

/* Initialize a UM980 instance.
 * Sets defaults: freshness_timeout = 2000ms, all counters = 0,
 * snapshot = invalid/stale. */
void gnss_um980_init(gnss_um980_t* rx, uint8_t instance_id, const char* name);

/* Set the RX source buffer (e.g., transport_uart.rx_buffer).
 * The source is NOT owned by this component. */
void gnss_um980_set_rx_source(gnss_um980_t* rx, byte_ring_buffer_t* source);

/* Set the freshness timeout in milliseconds.
 * Clamped to [GNSS_FRESHNESS_TIMEOUT_MS_MIN, GNSS_FRESHNESS_TIMEOUT_MS_MAX].
 * Default: GNSS_FRESHNESS_TIMEOUT_MS_DEFAULT (2000ms). */
void gnss_um980_set_freshness_timeout(gnss_um980_t* rx, uint32_t timeout_ms);

/* Feed raw bytes into the receiver's NMEA parser (manual API).
 * Returns number of complete valid sentences parsed.
 * Updates per-sentence data and sets dirty flags.
 * Snapshot is NOT rebuilt here — call gnss_um980_finalize_snapshot()
 * or use service_step() for automatic timestamp + rebuild. */
uint32_t gnss_um980_feed(gnss_um980_t* rx, const uint8_t* data, size_t length);

/* Finalize snapshot after feeding (for testing / manual use).
 * Rebuilds snapshot from dirty data using provided timestamp,
 * then runs freshness check.
 * In production, service_step() does this automatically. */
void gnss_um980_finalize_snapshot(gnss_um980_t* rx, uint64_t timestamp_ms);

/* Service step: consume bytes from rx_source, parse NMEA,
 * rebuild snapshot, check freshness.
 * Called by the runtime service loop (Core 0). */
void gnss_um980_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* ---- Fast Path Hooks (NAV-FIX-001 AP-B) ----
 * Called from task_fast on Core 1 at 100 Hz.
 * fast_input:  consume bytes from rx_source, parse NMEA.
 * fast_process: rebuild snapshot if dirty, check freshness.
 * These allow GNSS processing at deterministic 100 Hz on Core 1. */

void gnss_um980_fast_input(runtime_component_t* comp, const fast_cycle_context_t* ctx);
void gnss_um980_fast_process(runtime_component_t* comp, const fast_cycle_context_t* ctx);

/* Get pointer to latest GGA data. NULL if not valid. */
const nmea_gga_t* gnss_um980_get_gga(const gnss_um980_t* rx);

/* Get pointer to latest RMC data. NULL if not valid. */
const nmea_rmc_t* gnss_um980_get_rmc(const gnss_um980_t* rx);

/* Get pointer to latest GST data. NULL if not valid. */
const nmea_gst_t* gnss_um980_get_gst(const gnss_um980_t* rx);

/* Get pointer to unified GNSS snapshot. NULL if rx is NULL.
 * Snapshot is rebuilt automatically in service_step(). */
const gnss_snapshot_t* gnss_um980_get_snapshot(const gnss_um980_t* rx);

/* Get pointer to the position snapshot buffer (for wiring to consumers).
 * Consumers read gnss_snapshot_t from this buffer via snapshot_buffer_get().
 * NULL if rx is NULL. */
const snapshot_buffer_t* gnss_um980_get_position_snapshot(const gnss_um980_t* rx);

/* Check if position data is valid (position_valid flag from snapshot). */
bool gnss_um980_has_fix(const gnss_um980_t* rx);

/* Check if snapshot is fresh (within timeout). */
bool gnss_um980_is_fresh(const gnss_um980_t* rx);

#ifdef __cplusplus
}
#endif
