#pragma once
/* ========================================================================
 * gnss_dual_heading.h — Dual-Antenna Heading (NAV-HEADING-001 NACHARBEIT)
 *
 * Computes heading from two GNSS snapshots (primary + secondary UM980).
 * Uses local flat-earth (equirectangular) approximation for ~70 cm baseline.
 *
 * DATA FLOW:
 *   primary gnss_snapshot_t  ──┐
 *                              ├── gnss_dual_heading_calc → gnss_heading_snapshot_t
 *   secondary gnss_snapshot_t ──┘
 *
 * APPROXIMATION METHOD (documented — Pflicht 4):
 *   Local equirectangular projection:
 *     dlat_m = (lat2 - lat1) × 111320.0
 *     dlon_m = (lon2 - lon1) × 111320.0 × cos(avg_lat_rad)
 *   Valid for short baselines (< 10 m). For 70 cm, error < 0.01°.
 *   Future: High-precision geodesy (Vincenty) possible for longer baselines.
 *
 * MOUNTING CONVENTION (documented — Pflicht 5):
 *   heading = bearing(primary → secondary)  =  Antenna Line Heading
 *   This is NOT automatically the vehicle heading.
 *   Vehicle heading = antenna line heading + mounting_offset (Future Work).
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
#include "snapshot_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Heading Quality Levels (formalized enum — Pflicht 1) ----
 *
 * Quality is derived from:
 *   1. Baseline match vs. expected value (3 tiers)
 *   2. Fix quality of both receivers (RTK > DGPS > SINGLE)
 *   3. Timestamp synchronicity (delta between snapshots)
 */
typedef enum {
    HEADING_QUALITY_NONE = 0,     /* No heading available (invalid) */
    HEADING_QUALITY_BAD,          /* Usable but severely degraded (baseline far off or timestamp mismatch) */
    HEADING_QUALITY_DEGRADED,     /* Usable but not ideal (baseline in DEGRADED range or no RTK) */
    HEADING_QUALITY_GOOD,         /* Good heading solution (baseline in GOOD range, at least one RTK) */
    HEADING_QUALITY_EXCELLENT     /* Best: RTK fixed both + baseline GOOD + timestamps synchronized */
} gnss_heading_quality_t;

/* ---- Baseline Quality Tiers (Pflicht 1 — formalized ranges) ----
 *
 * Expected baseline: 0.70 m
 * GOOD:       0.65 – 0.75 m  (within ±7% of expected)
 * DEGRADED:   0.50 – 0.90 m  (within ±29% of expected)
 * BAD:        0.10 – 0.50 m OR 0.90 – 2.00 m (within absolute limits but far off)
 * INVALID:    < 0.10 m OR > 2.00 m (outside absolute limits)
 */
typedef enum {
    BASELINE_QUALITY_INVALID = 0,  /* Outside absolute limits */
    BASELINE_QUALITY_BAD,          /* Within absolute limits but far from expected */
    BASELINE_QUALITY_DEGRADED,     /* Acceptable but not ideal */
    BASELINE_QUALITY_GOOD          /* Within tight tolerance of expected */
} gnss_baseline_quality_t;

/* ---- Heading Status Reason (why heading is invalid — Pflicht 3) ----
 *
 * Precise reason codes for diagnostics.
 * "INVALID" prefix: position_valid=false or NULL
 * "STALE" prefix: position_valid=true but fresh=false
 */
typedef enum {
    HEADING_REASON_NONE = 0,              /* All OK */
    HEADING_REASON_PRIMARY_INVALID,       /* Primary NULL or position_valid=false */
    HEADING_REASON_SECONDARY_INVALID,     /* Secondary NULL or position_valid=false */
    HEADING_REASON_PRIMARY_STALE,         /* Primary position_valid=true but fresh=false */
    HEADING_REASON_SECONDARY_STALE,       /* Secondary position_valid=true but fresh=false */
    HEADING_REASON_TIMESTAMP_MISMATCH,    /* |primary.timestamp - secondary.timestamp| > max */
    HEADING_REASON_IDENTICAL_POS,         /* Primary and secondary positions identical */
    HEADING_REASON_BASELINE_INVALID,      /* Baseline outside absolute limits */
    HEADING_REASON_NO_FIX_PRIMARY,        /* Primary fix_quality = NONE or UNKNOWN */
    HEADING_REASON_NO_FIX_SECONDARY       /* Secondary fix_quality = NONE or UNKNOWN */
} gnss_heading_reason_t;

/* ---- Baseline Thresholds (documented, tested, reproducible — Pflicht 1) ---- */
#define HEADING_BASELINE_EXPECTED_M     0.70   /* Expected antenna baseline (meters) */

/* GOOD range: expected ± 7% → 0.651 .. 0.749 */
#define HEADING_BASELINE_GOOD_LOWER_M   0.65
#define HEADING_BASELINE_GOOD_UPPER_M   0.75

/* DEGRADED range: wider band → 0.50 .. 0.90 */
#define HEADING_BASELINE_DEGRADED_LOWER_M  0.50
#define HEADING_BASELINE_DEGRADED_UPPER_M  0.90

/* Absolute limits (outside = INVALID) */
#define HEADING_BASELINE_MIN_M          0.10   /* Absolute minimum (meters) */
#define HEADING_BASELINE_MAX_M          2.00   /* Absolute maximum (meters) */

/* ---- Timestamp Synchronicity (Pflicht 2) ---- */
#define HEADING_TIMESTAMP_DELTA_GOOD_MS    100   /* <= 100 ms: synchronized */
#define HEADING_TIMESTAMP_DELTA_MAX_MS     250   /* <= 250 ms: degraded, > 250 ms: invalid */

/* ---- Heading Freshness ---- */
#define HEADING_FRESHNESS_TIMEOUT_MS    2000   /* Heading freshness timeout (ms) */

/* ---- Heading Snapshot (output — Pflicht 7 sharpened model) ----
 *
 * Produced by gnss_dual_heading_calc from two GNSS snapshots.
 * Consumed by downstream consumers (AOG app, diagnostics).
 */
typedef struct {
    /* ---- Validity ---- */
    bool valid;              /* true: all plausibility checks passed */
    bool fresh;              /* true: valid AND both inputs fresh AND timestamps synchronized */

    /* ---- Heading Data ---- */
    double heading_deg;      /* heading 0..360 degrees (0=N, 90=E, 180=S, 270=W) */
    double baseline_m;       /* estimated distance between antennas (meters) */

    /* ---- Quality & Diagnostics ---- */
    gnss_heading_quality_t quality;        /* overall heading quality */
    gnss_baseline_quality_t baseline_quality; /* baseline match quality tier */
    gnss_heading_reason_t   reason;        /* reason if valid=false */
    uint64_t timestamp_ms;                 /* timestamp of last calculation (ms) */
    uint64_t timestamp_delta_ms;           /* |primary.timestamp - secondary.timestamp| (ms) */
    uint32_t calc_count;                   /* number of successful calculations */

    /* ---- Future Work: Mounting Offset ----
     * mounting_offset_deg: offset from antenna line heading to vehicle heading.
     * Will be configurable via installation profile in a future task.
     * Currently reserved, always 0.0. */
    double mounting_offset_deg;
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
    uint32_t freshness_timeout_ms;   /* heading freshness timeout (ms) */

    /* ---- Output (direct struct for internal use) ---- */
    gnss_heading_snapshot_t heading;

    /* ---- Output snapshot buffer (for consumers via snapshot_buffer API) ---- */
    snapshot_buffer_t  heading_snapshot;
    gnss_heading_snapshot_t heading_storage;

    /* ---- Internal state ---- */
    uint64_t last_primary_pos_ms;    /* timestamp of last primary position (ms) */
    uint64_t last_secondary_pos_ms;  /* timestamp of last secondary position (ms) */
} gnss_dual_heading_calc_t;

/* ---- API ---- */

/* Initialize dual heading calculator.
 * Sets defaults: baseline=0.70m, freshness=2000ms,
 * heading=invalid, all counters=0, mounting_offset=0.0. */
void gnss_dual_heading_init(gnss_dual_heading_calc_t* calc);

/* Set input GNSS snapshot references (NOT owned).
 * Both must be valid pointers; NULL to disconnect a source. */
void gnss_dual_heading_set_sources(gnss_dual_heading_calc_t* calc,
                                    const gnss_snapshot_t* primary,
                                    const gnss_snapshot_t* secondary);

/* Set the expected antenna baseline.
 * Default: HEADING_BASELINE_EXPECTED_M (0.70m). */
void gnss_dual_heading_set_baseline(gnss_dual_heading_calc_t* calc, double baseline_m);

/* Set the heading freshness timeout.
 * Clamped to [100, 30000] ms. Default: HEADING_FRESHNESS_TIMEOUT_MS (2000ms). */
void gnss_dual_heading_set_freshness_timeout(gnss_dual_heading_calc_t* calc, uint32_t ms);

/* Calculate heading from current snapshots (manual API for testing).
 * Updates heading snapshot with bearing, baseline, quality, and validity.
 * Uses current_ms for freshness check.
 *
 * STRICT FRESHNESS (Pflicht 3):
 *   valid=true requires: primary.position_valid AND secondary.position_valid
 *                         AND primary.fresh AND secondary.fresh
 *   If any is stale: valid=false with PRIMARY_STALE or SECONDARY_STALE reason.
 *
 * TIMESTAMP SYNC (Pflicht 2):
 *   |primary.timestamp - secondary.timestamp| is checked.
 *   <= GOOD_MS: no quality penalty
 *   <= MAX_MS: quality degraded to at most BAD
 *   > MAX_MS: valid=false, reason=TIMESTAMP_MISMATCH */
void gnss_dual_heading_calculate(gnss_dual_heading_calc_t* calc, uint64_t current_ms);

/* Service step: called by the runtime service loop.
 * Reads input snapshots, calculates heading, checks freshness. */
void gnss_dual_heading_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* Get latest heading snapshot. NULL if calc is NULL. */
const gnss_heading_snapshot_t* gnss_dual_heading_get(const gnss_dual_heading_calc_t* calc);

/* Check if heading is valid and fresh. */
bool gnss_dual_heading_is_fresh(const gnss_dual_heading_calc_t* calc);

/* Get pointer to heading snapshot buffer (for consumers).
 * Returns NULL if calc is NULL. */
const snapshot_buffer_t* gnss_dual_heading_get_snapshot(const gnss_dual_heading_calc_t* calc);

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

/* Classify baseline quality tier (exposed for testing).
 * Returns GOOD/DEGRADED/BAD/INVALID based on absolute and expected thresholds. */
gnss_baseline_quality_t gnss_dual_heading_classify_baseline(double baseline_m,
                                                             double expected_m);

#ifdef __cplusplus
}
#endif
