#pragma once
/* ========================================================================
 * gnss_dual_heading.h — Dual-Antenna Heading from Primary/Secondary GNSS
 *                        (NAV-HEADING-001)
 *
 * Computes heading from two GNSS snapshots (primary + secondary UM980).
 * Uses local flat-earth approximation for 70 cm antenna baseline.
 *
 * DATA FLOW:
 *   primary gnss_snapshot_t  ──┐
 *                              ├── gnss_dual_heading_calc → gnss_heading_snapshot_t
 *   secondary gnss_snapshot_t ──┘
 *
 * HARD RULES:
 *   - No UART I/O in this component
 *   - No PGN 214 output
 *   - No UM980 configuration
 *   - No app_core monolith
 *   - No steering changes
 *   - Heading component stays isolated
 *   - Consumes only gnss_snapshot_t (no direct gnss_um980 coupling)
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include "runtime_component.h"
#include "gnss_snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Heading Quality Levels ---- */
typedef enum {
    HEADING_QUALITY_NONE = 0,     /* No heading available */
    HEADING_QUALITY_POOR,         /* Baseline far off or low fix quality */
    HEADING_QUALITY_DEGRADED,     /* Usable but not ideal */
    HEADING_QUALITY_GOOD,         /* Good heading solution */
    HEADING_QUALITY_EXCELLENT     /* RTK fixed on both, baseline matches */
} gnss_heading_quality_t;

/* ---- Heading Status Reason (why heading is invalid/POOR) ---- */
typedef enum {
    HEADING_REASON_NONE = 0,         /* All OK */
    HEADING_REASON_NO_PRIMARY,       /* Primary snapshot NULL or not position_valid */
    HEADING_REASON_NO_SECONDARY,     /* Secondary snapshot NULL or not position_valid */
    HEADING_REASON_PRIMARY_STALE,    /* Primary not fresh */
    HEADING_REASON_SECONDARY_STALE,  /* Secondary not fresh */
    HEADING_REASON_IDENTICAL_POS,    /* Primary and secondary positions identical */
    HEADING_REASON_BASELINE_TOO_SMALL,  /* Baseline < minimum threshold */
    HEADING_REASON_BASELINE_TOO_LARGE,  /* Baseline > maximum threshold */
    HEADING_REASON_NO_FIX_PRIMARY,   /* Primary has no fix (fix_quality = NONE) */
    HEADING_REASON_NO_FIX_SECONDARY  /* Secondary has no fix (fix_quality = NONE) */
} gnss_heading_reason_t;

/* ---- Plausibility Thresholds ---- */
#define HEADING_BASELINE_EXPECTED_M     0.70   /* Expected antenna baseline (meters) */
#define HEADING_BASELINE_TOLERANCE_PCT  30.0   /* 30% tolerance band */
#define HEADING_BASELINE_MIN_M          0.10   /* Absolute minimum baseline (meters) */
#define HEADING_BASELINE_MAX_M          2.00   /* Absolute maximum baseline (meters) */
#define HEADING_FRESHNESS_TIMEOUT_MS    2000   /* Heading freshness timeout (ms) */

/* ---- Heading Snapshot (output) ----
 *
 * Produced by gnss_dual_heading_calc from two GNSS snapshots.
 * Consumed by downstream consumers (AOG app, diagnostics).
 */
typedef struct {
    /* ---- Validity ---- */
    bool valid;              /* true: heading is valid and usable */
    bool fresh;              /* true: valid and within freshness timeout */

    /* ---- Heading Data ---- */
    double heading_deg;      /* heading 0..360 degrees (0=N, 90=E, 180=S, 270=W) */
    double baseline_m;       /* estimated distance between antennas (meters) */

    /* ---- Quality & Diagnostics ---- */
    gnss_heading_quality_t quality;
    gnss_heading_reason_t   reason;     /* reason if valid=false or quality=POOR */
    uint64_t timestamp_ms;   /* timestamp of last heading calculation (ms) */
    uint32_t calc_count;     /* number of successful heading calculations */
} gnss_heading_snapshot_t;

/* ---- Dual Heading Calculator ----
 *
 * Consumes two gnss_snapshot_t pointers (primary, secondary).
 * Produces gnss_heading_snapshot_t with heading, quality, and plausibility.
 *
 * The calculator does NOT own the input snapshots.
 * Snapshots are provided by gnss_um980_get_snapshot() or similar.
 */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    /* ---- Input references (set by caller, NOT owned) ---- */
    const gnss_snapshot_t* primary;
    const gnss_snapshot_t* secondary;

    /* ---- Configuration ---- */
    double baseline_expected_m;      /* expected antenna baseline (meters) */
    double baseline_tolerance_pct;   /* tolerance band percentage (0..100) */
    uint32_t freshness_timeout_ms;   /* heading freshness timeout (ms) */

    /* ---- Output ---- */
    gnss_heading_snapshot_t heading;

    /* ---- Internal state ---- */
    uint64_t last_primary_pos_ms;    /* timestamp of last primary position (ms) */
    uint64_t last_secondary_pos_ms;  /* timestamp of last secondary position (ms) */
} gnss_dual_heading_calc_t;

/* ---- API ---- */

/* Initialize dual heading calculator.
 * Sets defaults: baseline=0.70m, tolerance=30%, freshness=2000ms,
 * heading=invalid, all counters=0. */
void gnss_dual_heading_init(gnss_dual_heading_calc_t* calc);

/* Set input GNSS snapshot references (NOT owned).
 * Both must be valid pointers; NULL to disconnect a source. */
void gnss_dual_heading_set_sources(gnss_dual_heading_calc_t* calc,
                                    const gnss_snapshot_t* primary,
                                    const gnss_snapshot_t* secondary);

/* Set the expected antenna baseline.
 * Default: HEADING_BASELINE_EXPECTED_M (0.70m). */
void gnss_dual_heading_set_baseline(gnss_dual_heading_calc_t* calc, double baseline_m);

/* Set the baseline tolerance percentage.
 * Default: HEADING_BASELINE_TOLERANCE_PCT (30%). Clamped to [1, 100]. */
void gnss_dual_heading_set_baseline_tolerance(gnss_dual_heading_calc_t* calc, double pct);

/* Set the heading freshness timeout.
 * Clamped to [100, 30000] ms. Default: HEADING_FRESHNESS_TIMEOUT_MS (2000ms). */
void gnss_dual_heading_set_freshness_timeout(gnss_dual_heading_calc_t* calc, uint32_t ms);

/* Calculate heading from current snapshots (manual API for testing).
 * Updates heading snapshot with bearing, baseline, quality, and validity.
 * Uses current_ms for freshness check. */
void gnss_dual_heading_calculate(gnss_dual_heading_calc_t* calc, uint64_t current_ms);

/* Service step: called by the runtime service loop.
 * Reads input snapshots, calculates heading, checks freshness. */
void gnss_dual_heading_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* Get latest heading snapshot. NULL if calc is NULL. */
const gnss_heading_snapshot_t* gnss_dual_heading_get(const gnss_dual_heading_calc_t* calc);

/* Check if heading is valid and fresh. */
bool gnss_dual_heading_is_fresh(const gnss_dual_heading_calc_t* calc);

/* ---- Utility: Bearing calculation (exposed for testing) ---- */

/* Calculate bearing from position1 to position2 in degrees (0..360, 0=N).
 * Uses local flat-earth approximation.
 * lat1/lat2: decimal degrees, lon1/lon2: decimal degrees.
 * Returns: bearing in degrees [0, 360). */
double gnss_dual_heading_bearing(double lat1, double lon1, double lat2, double lon2);

/* Calculate distance between two positions using flat-earth approximation (meters).
 * lat1/lat2: decimal degrees, lon1/lon2: decimal degrees.
 * Returns: distance in meters. */
double gnss_dual_heading_distance_m(double lat1, double lon1, double lat2, double lon2);

#ifdef __cplusplus
}
#endif
