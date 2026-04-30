/* ========================================================================
 * gnss_dual_heading.c — Dual-Antenna Heading Implementation
 *                     (NAV-HEADING-001 NACHARBEIT)
 *
 * Computes heading from two GNSS positions using local flat-earth
 * (equirectangular) approximation. Suitable for short baselines (~70 cm).
 *
 * APPROXIMATION METHOD (Pflicht 4 — documented):
 *   Local equirectangular projection:
 *     dlat_m = (lat2 - lat1) × 111320.0  (meters per degree latitude at equator)
 *     dlon_m = (lon2 - lon1) × 111320.0 × cos(avg_lat_rad)
 *
 *   Valid for short baselines (< 10 m). At 70 cm the angular error is
 *   negligible (< 0.01 degrees). For longer baselines, a geodetic
 *   algorithm (Vincenty) should be used.
 *
 *   No geodetic long-range calculation is performed.
 *   Future: High-precision geodesy is possible as an upgrade path.
 *
 * MOUNTING CONVENTION (Pflicht 5 — documented):
 *   heading = bearing(primary → secondary)
 *   This is the ANTENNA LINE HEADING, not the vehicle heading.
 *   Vehicle heading = antenna line heading + mounting_offset.
 *   mounting_offset_deg is reserved for future installation profiles.
 *
 * PLAUSIBILITY CHECKS (all mandatory):
 *   1. Both snapshots non-NULL and position_valid
 *   2. Both snapshots fresh (strict — Pflicht 3)
 *   3. Timestamp delta within limit (Pflicht 2)
 *   4. Both fix qualities usable (> NONE)
 *   5. Positions not identical
 *   6. Baseline within absolute limits [MIN, MAX]
 *   7. Baseline quality tier assessed (Pflicht 1)
 *
 * BASELINE QUALITY TIERS (Pflicht 1 — documented, tested, reproducible):
 *   GOOD:       0.65 – 0.75 m  (expected 0.70m ± 7%)
 *   DEGRADED:   0.50 – 0.90 m  (wider band)
 *   BAD:        0.10 – 0.50 m OR 0.90 – 2.00 m
 *   INVALID:    < 0.10 m OR > 2.00 m
 *
 * TIMESTAMP SYNCHRONICITY (Pflicht 2):
 *   |primary.timestamp - secondary.timestamp|
 *   <= 100 ms: GOOD (no penalty)
 *   <= 250 ms: heading quality capped at BAD
 *   >  250 ms: valid=false, reason=TIMESTAMP_MISMATCH
 *
 * STRICT FRESHNESS (Pflicht 3):
 *   valid requires ALL of:
 *     primary.position_valid = true
 *     secondary.position_valid = true
 *     primary.fresh = true
 *     secondary.fresh = true
 *   If position_valid=false → INVALID reason
 *   If position_valid=true but fresh=false → STALE reason
 * ======================================================================== */

#include "gnss_dual_heading.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Meters per degree latitude at WGS-84 equator ---- */
#define METERS_PER_DEG_LAT  111320.0

/* ---- Internal: check if fix quality is usable (anything better than NONE) ---- */
static bool has_usable_fix(gnss_fix_quality_t fq)
{
    return (fq != GNSS_FIX_NONE && fq != GNSS_FIX_UNKNOWN);
}

/* ---- Internal: compute absolute timestamp delta (handles wrap) ---- */
static uint64_t compute_timestamp_delta(uint64_t ts_a, uint64_t ts_b)
{
    /* Clock wrap protection: if ts_a < ts_b, assume no wrap (both from same epoch).
     * For 64-bit ms timestamps, wrap is not a realistic concern. */
    if (ts_a >= ts_b) {
        return ts_a - ts_b;
    }
    return ts_b - ts_a;
}

/* ========================================================================
 * Baseline Quality Classification (Pflicht 1)
 * ======================================================================== */

gnss_baseline_quality_t gnss_dual_heading_classify_baseline(double baseline_m,
                                                             double expected_m)
{
    /* INVALID: outside absolute limits */
    if (baseline_m < HEADING_BASELINE_MIN_M || baseline_m > HEADING_BASELINE_MAX_M) {
        return BASELINE_QUALITY_INVALID;
    }

    /* GOOD: within tight band around expected */
    /* GOOD range uses fixed thresholds (0.65-0.75) for 0.70m expected.
     * For custom expected values, use ±7% band. */
    double good_lower = HEADING_BASELINE_GOOD_LOWER_M;
    double good_upper = HEADING_BASELINE_GOOD_UPPER_M;

    /* If custom expected baseline, compute GOOD as ±7% */
    if (expected_m != HEADING_BASELINE_EXPECTED_M) {
        good_lower = expected_m * 0.93;  /* -7% */
        good_upper = expected_m * 1.07;  /* +7% */
    }

    if (baseline_m >= good_lower && baseline_m <= good_upper) {
        return BASELINE_QUALITY_GOOD;
    }

    /* DEGRADED: wider band */
    double degraded_lower = HEADING_BASELINE_DEGRADED_LOWER_M;
    double degraded_upper = HEADING_BASELINE_DEGRADED_UPPER_M;

    /* If custom expected baseline, compute DEGRADED as ±29% */
    if (expected_m != HEADING_BASELINE_EXPECTED_M) {
        degraded_lower = expected_m * 0.71;  /* -29% */
        degraded_upper = expected_m * 1.29;  /* +29% */
    }

    if (baseline_m >= degraded_lower && baseline_m <= degraded_upper) {
        return BASELINE_QUALITY_DEGRADED;
    }

    /* BAD: within absolute limits but outside both bands */
    return BASELINE_QUALITY_BAD;
}

/* ---- Internal: assess overall heading quality (Pflicht 1 refined) ----
 *
 * Quality is determined by:
 *   1. Baseline quality tier (GOOD/DEGRADED/BAD)
 *   2. Fix quality of both receivers (RTK > DGPS > SINGLE)
 *   3. Timestamp synchronicity (checked separately before this is called)
 */
static gnss_heading_quality_t assess_heading_quality(
    gnss_fix_quality_t primary_fq,
    gnss_fix_quality_t secondary_fq,
    gnss_baseline_quality_t bl_quality,
    bool timestamps_synced)
{
    bool both_rtk_fixed = (primary_fq == GNSS_FIX_RTK_FIXED &&
                           secondary_fq == GNSS_FIX_RTK_FIXED);
    bool any_rtk = (primary_fq == GNSS_FIX_RTK_FIXED || primary_fq == GNSS_FIX_RTK_FLOAT ||
                    secondary_fq == GNSS_FIX_RTK_FIXED || secondary_fq == GNSS_FIX_RTK_FLOAT);

    /* Timestamp desync caps quality at BAD */
    if (!timestamps_synced) {
        return HEADING_QUALITY_BAD;
    }

    /* Baseline BAD caps quality at BAD regardless of fix quality */
    if (bl_quality == BASELINE_QUALITY_BAD) {
        return HEADING_QUALITY_BAD;
    }

    /* EXCELLENT: RTK fixed on both + GOOD baseline */
    if (both_rtk_fixed && bl_quality == BASELINE_QUALITY_GOOD) {
        return HEADING_QUALITY_EXCELLENT;
    }

    /* GOOD: any RTK + baseline at least DEGRADED */
    if (any_rtk && bl_quality >= BASELINE_QUALITY_DEGRADED) {
        return HEADING_QUALITY_GOOD;
    }

    /* GOOD: both RTK (any) + GOOD baseline */
    if (both_rtk_fixed && bl_quality == BASELINE_QUALITY_GOOD) {
        return HEADING_QUALITY_EXCELLENT;
    }

    /* DEGRADED: usable fix + baseline at least DEGRADED */
    if (has_usable_fix(primary_fq) && has_usable_fix(secondary_fq) &&
        bl_quality >= BASELINE_QUALITY_DEGRADED) {
        if (any_rtk) {
            return HEADING_QUALITY_GOOD;
        }
        return HEADING_QUALITY_DEGRADED;
    }

    /* DEGRADED fallback: any usable fix */
    if (has_usable_fix(primary_fq) && has_usable_fix(secondary_fq)) {
        return HEADING_QUALITY_DEGRADED;
    }

    return HEADING_QUALITY_BAD;
}

/* ========================================================================
 * Public Utility Functions
 * ======================================================================== */

double gnss_dual_heading_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    double dlat = lat2 - lat1;
    double avg_lat_rad = (lat1 + lat2) / 2.0 * M_PI / 180.0;
    double dlat_m = dlat * METERS_PER_DEG_LAT;
    double dlon_m = (lon2 - lon1) * METERS_PER_DEG_LAT * cos(avg_lat_rad);
    return sqrt(dlat_m * dlat_m + dlon_m * dlon_m);
}

double gnss_dual_heading_bearing(double lat1, double lon1, double lat2, double lon2)
{
    double dlat = lat2 - lat1;
    double dlon = lon2 - lon1;
    double avg_lat_rad = (lat1 + lat2) / 2.0 * M_PI / 180.0;
    double dlat_m = dlat * METERS_PER_DEG_LAT;
    double dlon_m = dlon * METERS_PER_DEG_LAT * cos(avg_lat_rad);

    /* atan2(east, north) — 0 = north, positive clockwise */
    double bearing = atan2(dlon_m, dlat_m) * 180.0 / M_PI;

    /* Normalize to [0, 360) */
    if (bearing < 0.0) {
        bearing += 360.0;
    }
    if (bearing >= 360.0) {
        bearing -= 360.0;
    }

    return bearing;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

void gnss_dual_heading_init(gnss_dual_heading_calc_t* calc)
{
    if (calc == NULL) {
        return;
    }
    memset(calc, 0, sizeof(gnss_dual_heading_calc_t));
    calc->baseline_expected_m = HEADING_BASELINE_EXPECTED_M;
    calc->freshness_timeout_ms = HEADING_FRESHNESS_TIMEOUT_MS;
    calc->heading.valid = false;
    calc->heading.fresh = false;
    calc->heading.quality = HEADING_QUALITY_NONE;
    calc->heading.baseline_quality = BASELINE_QUALITY_INVALID;
    calc->heading.reason = HEADING_REASON_NONE;
    calc->heading.mounting_offset_deg = 0.0;
    snapshot_buffer_init(&calc->heading_snapshot, &calc->heading_storage,
                         sizeof(gnss_heading_snapshot_t));
    calc->component.service_step = gnss_dual_heading_service_step;
}

void gnss_dual_heading_set_sources(gnss_dual_heading_calc_t* calc,
                                    const gnss_snapshot_t* primary,
                                    const gnss_snapshot_t* secondary)
{
    if (calc == NULL) {
        return;
    }
    calc->primary = primary;
    calc->secondary = secondary;
}

void gnss_dual_heading_set_baseline(gnss_dual_heading_calc_t* calc, double baseline_m)
{
    if (calc == NULL) {
        return;
    }
    if (baseline_m > 0.0) {
        calc->baseline_expected_m = baseline_m;
    }
}

void gnss_dual_heading_set_freshness_timeout(gnss_dual_heading_calc_t* calc, uint32_t ms)
{
    if (calc == NULL) {
        return;
    }
    if (ms < 100) ms = 100;
    if (ms > 30000) ms = 30000;
    calc->freshness_timeout_ms = ms;
}

void gnss_dual_heading_calculate(gnss_dual_heading_calc_t* calc, uint64_t current_ms)
{
    if (calc == NULL) {
        return;
    }

    /* Default: invalid */
    calc->heading.valid = false;
    calc->heading.fresh = false;
    calc->heading.heading_deg = 0.0;
    calc->heading.baseline_m = 0.0;
    calc->heading.quality = HEADING_QUALITY_NONE;
    calc->heading.baseline_quality = BASELINE_QUALITY_INVALID;
    calc->heading.timestamp_ms = current_ms;
    calc->heading.timestamp_delta_ms = 0;

    /* ==================================================================
     * Pflicht 3: STRICT FRESHNESS
     * valid requires ALL of: position_valid + fresh on both
     * ================================================================== */

    /* ---- Check 1: Primary snapshot available and position_valid ---- */
    if (calc->primary == NULL || !calc->primary->position_valid) {
        calc->heading.reason = HEADING_REASON_PRIMARY_INVALID;
        goto publish;
    }

    /* ---- Check 2: Secondary snapshot available and position_valid ---- */
    if (calc->secondary == NULL || !calc->secondary->position_valid) {
        calc->heading.reason = HEADING_REASON_SECONDARY_INVALID;
        goto publish;
    }

    /* ---- Check 3: Primary fresh (STRICT — Pflicht 3) ---- */
    if (!calc->primary->fresh) {
        calc->heading.reason = HEADING_REASON_PRIMARY_STALE;
        goto publish;
    }

    /* ---- Check 4: Secondary fresh (STRICT — Pflicht 3) ---- */
    if (!calc->secondary->fresh) {
        calc->heading.reason = HEADING_REASON_SECONDARY_STALE;
        goto publish;
    }

    /* ---- Check 5: Primary has usable fix quality ---- */
    if (!has_usable_fix(calc->primary->fix_quality)) {
        calc->heading.reason = HEADING_REASON_NO_FIX_PRIMARY;
        goto publish;
    }

    /* ---- Check 6: Secondary has usable fix quality ---- */
    if (!has_usable_fix(calc->secondary->fix_quality)) {
        calc->heading.reason = HEADING_REASON_NO_FIX_SECONDARY;
        goto publish;
    }

    /* ==================================================================
     * Pflicht 2: TIMESTAMP SYNCHRONICITY
     * ================================================================== */
    {
        uint64_t ts_delta = compute_timestamp_delta(
            calc->primary->timestamp_ms, calc->secondary->timestamp_ms);
        calc->heading.timestamp_delta_ms = ts_delta;

        if (ts_delta > HEADING_TIMESTAMP_DELTA_MAX_MS) {
            calc->heading.reason = HEADING_REASON_TIMESTAMP_MISMATCH;
            goto publish;
        }
    }

    /* ==================================================================
     * Position and Baseline Checks
     * ================================================================== */

    {
        double lat1 = calc->primary->latitude;
        double lon1 = calc->primary->longitude;
        double lat2 = calc->secondary->latitude;
        double lon2 = calc->secondary->longitude;

        /* ---- Check 7: Positions not identical ---- */
        if (lat1 == lat2 && lon1 == lon2) {
            calc->heading.reason = HEADING_REASON_IDENTICAL_POS;
            goto publish;
        }

        /* ---- Calculate baseline distance ---- */
        double baseline = gnss_dual_heading_distance_m(lat1, lon1, lat2, lon2);

        /* ---- Check 8: Baseline within absolute limits ---- */
        gnss_baseline_quality_t bl_quality = gnss_dual_heading_classify_baseline(
            baseline, calc->baseline_expected_m);

        if (bl_quality == BASELINE_QUALITY_INVALID) {
            calc->heading.baseline_m = baseline;
            calc->heading.baseline_quality = bl_quality;
            calc->heading.reason = HEADING_REASON_BASELINE_INVALID;
            goto publish;
        }

        /* ==================================================================
         * All checks passed — compute heading and quality
         * ================================================================== */

        bool timestamps_synced =
            (calc->heading.timestamp_delta_ms <= HEADING_TIMESTAMP_DELTA_GOOD_MS);

        double heading = gnss_dual_heading_bearing(lat1, lon1, lat2, lon2);

        /* Assess quality (baseline tier + fix quality + timestamp sync) */
        gnss_heading_quality_t quality = assess_heading_quality(
            calc->primary->fix_quality,
            calc->secondary->fix_quality,
            bl_quality,
            timestamps_synced);

        /* Store results */
        calc->heading.heading_deg = heading;
        calc->heading.baseline_m = baseline;
        calc->heading.baseline_quality = bl_quality;
        calc->heading.quality = quality;
        calc->heading.valid = true;
        calc->heading.fresh = true;  /* All inputs fresh, all checks passed */
        calc->heading.calc_count++;
        calc->last_primary_pos_ms = calc->primary->timestamp_ms;
        calc->last_secondary_pos_ms = calc->secondary->timestamp_ms;
        calc->heading.reason = HEADING_REASON_NONE;
        calc->heading.mounting_offset_deg = 0.0;  /* Reserved for future use */
    }

publish:
    /* Always publish to snapshot buffer so consumers see latest state
     * (including reason codes when invalid) */
    snapshot_buffer_set(&calc->heading_snapshot, &calc->heading);
}

void gnss_dual_heading_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    gnss_dual_heading_calc_t* calc = (gnss_dual_heading_calc_t*)comp;
    if (calc == NULL) {
        return;
    }

    uint64_t ts_ms = timestamp_us / 1000;
    gnss_dual_heading_calculate(calc, ts_ms);
}

const gnss_heading_snapshot_t* gnss_dual_heading_get(const gnss_dual_heading_calc_t* calc)
{
    if (calc == NULL) {
        return NULL;
    }
    return &calc->heading;
}

bool gnss_dual_heading_is_fresh(const gnss_dual_heading_calc_t* calc)
{
    if (calc == NULL) {
        return false;
    }
    return calc->heading.valid && calc->heading.fresh;
}

const snapshot_buffer_t* gnss_dual_heading_get_snapshot(const gnss_dual_heading_calc_t* calc)
{
    if (calc == NULL) {
        return NULL;
    }
    return &calc->heading_snapshot;
}
