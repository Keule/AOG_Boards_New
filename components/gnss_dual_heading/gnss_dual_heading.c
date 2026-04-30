/* ========================================================================
 * gnss_dual_heading.c — Dual-Antenna Heading Implementation (NAV-HEADING-001)
 *
 * Computes heading from two GNSS positions using local flat-earth
 * approximation. Suitable for short baselines (~70 cm).
 *
 * BEARING FORMULA:
 *   dlat_m = (lat2 - lat1) * 111320.0
 *   dlon_m = (lon2 - lon1) * 111320.0 * cos(avg_lat_rad)
 *   bearing = atan2(dlon_m, dlat_m)  (east from north)
 *   if bearing < 0: bearing += 360.0
 *
 * LIMITATION:
 *   This is a LOCAL flat-earth approximation, not a geodetically precise
 *   Vincenty/geodesic calculation. For the 70 cm baseline, the error is
 *   negligible (< 0.01 degrees). For baselines > 10 m, consider switching
 *   to a geodetic algorithm.
 *
 * PLAUSIBILITY CHECKS:
 *   1. Both snapshots must be non-NULL and position_valid
 *   2. Both snapshots must be fresh (within freshness timeout)
 *   3. Both fix qualities must be > NONE
 *   4. Positions must not be identical (dlat=0 && dlon=0)
 *   5. Estimated baseline must be within [BASELINE_MIN, BASELINE_MAX]
 *   6. Baseline quality assessed vs. expected value ± tolerance
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

/* ---- Internal: determine heading quality based on inputs ---- */
static gnss_heading_quality_t assess_quality(
    gnss_fix_quality_t primary_fq,
    gnss_fix_quality_t secondary_fq,
    double baseline_m,
    double expected_m,
    double tolerance_pct)
{
    bool both_rtk_fixed = (primary_fq == GNSS_FIX_RTK_FIXED &&
                           secondary_fq == GNSS_FIX_RTK_FIXED);
    bool any_rtk = (primary_fq == GNSS_FIX_RTK_FIXED || primary_fq == GNSS_FIX_RTK_FLOAT ||
                    secondary_fq == GNSS_FIX_RTK_FIXED || secondary_fq == GNSS_FIX_RTK_FLOAT);
    bool any_dgps = (primary_fq == GNSS_FIX_DGPS || secondary_fq == GNSS_FIX_DGPS);

    /* Baseline plausibility: check if within tolerance band */
    double lower = expected_m * (1.0 - tolerance_pct / 100.0);
    double upper = expected_m * (1.0 + tolerance_pct / 100.0);
    bool baseline_ok = (baseline_m >= lower && baseline_m <= upper);

    /* Determine quality */
    if (both_rtk_fixed && baseline_ok) {
        return HEADING_QUALITY_EXCELLENT;
    }
    if ((both_rtk_fixed || any_rtk) && baseline_ok) {
        return HEADING_QUALITY_GOOD;
    }
    if (has_usable_fix(primary_fq) && has_usable_fix(secondary_fq)) {
        if (baseline_ok || any_rtk || any_dgps) {
            return HEADING_QUALITY_DEGRADED;
        }
        return HEADING_QUALITY_POOR;
    }
    return HEADING_QUALITY_POOR;
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
    calc->baseline_tolerance_pct = HEADING_BASELINE_TOLERANCE_PCT;
    calc->freshness_timeout_ms = HEADING_FRESHNESS_TIMEOUT_MS;
    calc->heading.valid = false;
    calc->heading.fresh = false;
    calc->heading.quality = HEADING_QUALITY_NONE;
    calc->heading.reason = HEADING_REASON_NONE;
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

void gnss_dual_heading_set_baseline_tolerance(gnss_dual_heading_calc_t* calc, double pct)
{
    if (calc == NULL) {
        return;
    }
    if (pct < 1.0) pct = 1.0;
    if (pct > 100.0) pct = 100.0;
    calc->baseline_tolerance_pct = pct;
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
    calc->heading.timestamp_ms = current_ms;

    /* ---- Check 1: Primary snapshot available and position_valid ---- */
    if (calc->primary == NULL || !calc->primary->position_valid) {
        calc->heading.reason = HEADING_REASON_NO_PRIMARY;
        return;
    }

    /* ---- Check 2: Secondary snapshot available and position_valid ---- */
    if (calc->secondary == NULL || !calc->secondary->position_valid) {
        calc->heading.reason = HEADING_REASON_NO_SECONDARY;
        return;
    }

    /* ---- Check 3: Primary has usable fix quality ---- */
    if (!has_usable_fix(calc->primary->fix_quality)) {
        calc->heading.reason = HEADING_REASON_NO_FIX_PRIMARY;
        return;
    }

    /* ---- Check 4: Secondary has usable fix quality ---- */
    if (!has_usable_fix(calc->secondary->fix_quality)) {
        calc->heading.reason = HEADING_REASON_NO_FIX_SECONDARY;
        return;
    }

    /* ---- Check 5: Positions not identical ---- */
    double lat1 = calc->primary->latitude;
    double lon1 = calc->primary->longitude;
    double lat2 = calc->secondary->latitude;
    double lon2 = calc->secondary->longitude;

    if (lat1 == lat2 && lon1 == lon2) {
        calc->heading.reason = HEADING_REASON_IDENTICAL_POS;
        return;
    }

    /* ---- Calculate baseline distance ---- */
    double baseline = gnss_dual_heading_distance_m(lat1, lon1, lat2, lon2);

    /* ---- Check 6: Baseline minimum ---- */
    if (baseline < HEADING_BASELINE_MIN_M) {
        calc->heading.baseline_m = baseline;
        calc->heading.reason = HEADING_REASON_BASELINE_TOO_SMALL;
        return;
    }

    /* ---- Check 7: Baseline maximum ---- */
    if (baseline > HEADING_BASELINE_MAX_M) {
        calc->heading.baseline_m = baseline;
        calc->heading.reason = HEADING_REASON_BASELINE_TOO_LARGE;
        return;
    }

    /* ---- Calculate heading ---- */
    double heading = gnss_dual_heading_bearing(lat1, lon1, lat2, lon2);

    /* ---- Assess quality ---- */
    gnss_heading_quality_t quality = assess_quality(
        calc->primary->fix_quality,
        calc->secondary->fix_quality,
        baseline,
        calc->baseline_expected_m,
        calc->baseline_tolerance_pct);

    /* ---- Store results ---- */
    calc->heading.heading_deg = heading;
    calc->heading.baseline_m = baseline;
    calc->heading.quality = quality;
    calc->heading.valid = true;
    calc->heading.calc_count++;
    calc->last_primary_pos_ms = calc->primary->timestamp_ms;
    calc->last_secondary_pos_ms = calc->secondary->timestamp_ms;
    calc->heading.reason = HEADING_REASON_NONE;

    /* ---- Freshness check ---- */
    /* Heading is fresh if both input snapshots are fresh */
    calc->heading.fresh = calc->primary->fresh && calc->secondary->fresh;

    /* Also check our own freshness timeout based on heading timestamp */
    if (calc->heading.fresh && calc->heading.timestamp_ms > 0) {
        if (current_ms > calc->heading.timestamp_ms) {
            uint64_t age = current_ms - calc->heading.timestamp_ms;
            if (age > calc->freshness_timeout_ms) {
                calc->heading.fresh = false;
            }
        }
    }
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
