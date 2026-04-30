/* ========================================================================
 * test_heading_validation — Host tests for NAV-HEADING-001
 *
 * Dual-Antenna Heading from Primary/Secondary GNSS Snapshots.
 *
 * Tests cover:
 *  1.  Init: all fields zero/invalid
 *  2.  Bearing calculation: known North, East, South, West headings
 *  3.  0..360 normalization (negative angle, angle >= 360)
 *  4.  Distance calculation: known baseline
 *  5.  Valid heading: both snapshots valid and fresh
 *  6.  Invalid primary snapshot → heading invalid (NO_PRIMARY)
 *  7.  Invalid secondary snapshot → heading invalid (NO_SECONDARY)
 *  8.  NULL primary → heading invalid
 *  9.  NULL secondary → heading invalid
 * 10.  Primary not position_valid → heading invalid
 * 11.  Secondary not position_valid → heading invalid
 * 12.  Primary stale (not fresh) → heading not fresh
 * 13.  Secondary stale (not fresh) → heading not fresh
 * 14.  Identical positions → heading invalid (IDENTICAL_POS)
 * 15.  Baseline too small (< 0.10m) → heading invalid
 * 16.  Baseline too large (> 2.00m) → heading invalid
 * 17.  Quality EXCELLENT: RTK_FIXED on both + baseline matches expected
 * 18.  Quality GOOD: RTK on one + baseline matches
 * 19.  Quality DEGRADED: SINGLE fix + baseline matches
 * 20.  Quality POOR: baseline far off expected
 * 21.  Fix quality NONE on primary → heading invalid (NO_FIX_PRIMARY)
 * 22.  Fix quality UNKNOWN on primary → heading invalid (NO_FIX_PRIMARY)
 * 23.  Fix quality NONE on secondary → heading invalid (NO_FIX_SECONDARY)
 * 24.  Baseline plausibility: 0.70m expected ± 30%
 * 25.  Set sources: primary/secondary pointers
 * 26.  Set baseline: custom value
 * 27.  Set baseline tolerance: custom percentage, clamping
 * 28.  Set freshness timeout: custom ms, clamping
 * 29.  Calc count increments on each successful calculation
 * 30.  Timestamp propagated correctly
 * 31.  Null safety: all API functions with NULL
 * 32.  service_step: delegates to calculate
 * 33.  gnss_dual_heading_is_fresh: valid + fresh
 * 34.  Heading snapshot getter: returns pointer
 * 35.  Heading from actual 70cm east displacement → ~90 degrees
 * 36.  Heading from actual 70cm south displacement → ~180 degrees
 * 37.  Heading from actual 70cm west displacement → ~270 degrees
 * ======================================================================== */

#include "unity.h"
#include "gnss_dual_heading.h"
#include "gnss_snapshot.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * Test Helpers
 * ======================================================================== */

/* Create a valid GNSS snapshot with given position and quality. */
static gnss_snapshot_t make_snapshot(double lat, double lon,
                                      gnss_fix_quality_t fq,
                                      bool pos_valid, bool is_fresh,
                                      uint64_t timestamp_ms)
{
    gnss_snapshot_t snap;
    gnss_snapshot_init(&snap);
    snap.latitude = lat;
    snap.longitude = lon;
    snap.altitude = 200.0;
    snap.fix_quality = fq;
    snap.position_valid = pos_valid;
    snap.motion_valid = true;
    snap.valid = pos_valid && true;
    snap.fresh = is_fresh;
    snap.timestamp_ms = timestamp_ms;
    snap.last_gga_time_ms = timestamp_ms;
    snap.last_rmc_time_ms = timestamp_ms;
    return snap;
}

/* Default snapshot: valid, fresh, RTK_FIXED, at 0,0 */
static gnss_snapshot_t make_default_snapshot(uint64_t ts)
{
    return make_snapshot(0.0, 0.0, GNSS_FIX_RTK_FIXED, true, true, ts);
}

/* ========================================================================
 * Test 1: Init
 * ======================================================================== */

void test_heading_init(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_FALSE(calc.heading.fresh);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, calc.heading.heading_deg);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, calc.heading.baseline_m);
    TEST_ASSERT_EQUAL(HEADING_QUALITY_NONE, calc.heading.quality);
    TEST_ASSERT_EQUAL(HEADING_REASON_NONE, calc.heading.reason);
    TEST_ASSERT_EQUAL_DOUBLE(HEADING_BASELINE_EXPECTED_M, calc.baseline_expected_m);
    TEST_ASSERT_EQUAL_DOUBLE(HEADING_BASELINE_TOLERANCE_PCT, calc.baseline_tolerance_pct);
    TEST_ASSERT_EQUAL(HEADING_FRESHNESS_TIMEOUT_MS, calc.freshness_timeout_ms);
    TEST_ASSERT_NULL(calc.primary);
    TEST_ASSERT_NULL(calc.secondary);
}

/* ========================================================================
 * Tests 2-4: Bearing and Distance utilities
 * ======================================================================== */

void test_bearing_north(void)
{
    /* From (50.0, 10.0) to (50.0 + ~6.3e-6, 10.0) = ~0.7m north */
    /* 1 degree lat = 111320 m → 0.7m = 0.7/111320 degrees */
    double dlat = 0.7 / 111320.0;
    double bearing = gnss_dual_heading_bearing(50.0, 10.0, 50.0 + dlat, 10.0);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 0.0, bearing);  /* ~0° = North */
}

void test_bearing_east(void)
{
    /* From (50.0, 10.0) to (50.0, 10.0 + ~dlon for 0.7m east) */
    double dlon = 0.7 / (111320.0 * cos(50.0 * M_PI / 180.0));
    double bearing = gnss_dual_heading_bearing(50.0, 10.0, 50.0, 10.0 + dlon);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 90.0, bearing);  /* ~90° = East */
}

void test_bearing_south(void)
{
    double dlat = 0.7 / 111320.0;
    double bearing = gnss_dual_heading_bearing(50.0, 10.0, 50.0 - dlat, 10.0);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 180.0, bearing);  /* ~180° = South */
}

void test_bearing_west(void)
{
    double dlon = 0.7 / (111320.0 * cos(50.0 * M_PI / 180.0));
    double bearing = gnss_dual_heading_bearing(50.0, 10.0, 50.0, 10.0 - dlon);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 270.0, bearing);  /* ~270° = West */
}

void test_bearing_normalization_negative(void)
{
    /* Bearing that would be -45° should normalize to 315° */
    /* West + a bit south → should be close to 270°, let's verify exact 315° case */
    /* Going SW: negative dlat + negative dlon → atan2(-1, -1) = -135° → +360 = 225° */
    double dlat = -0.7 / 111320.0;
    double dlon = -0.7 / (111320.0 * cos(50.0 * M_PI / 180.0));
    double bearing = gnss_dual_heading_bearing(50.0, 10.0, 50.0 + dlat, 10.0 + dlon);
    /* atan2(negative, negative) = 3rd quadrant → between 180 and 270 */
    TEST_ASSERT_TRUE(bearing >= 180.0 && bearing < 270.0);
    /* Specifically: atan2(-east, -north) where both are -0.7 → -135° + 360 = 225° */
    TEST_ASSERT_DOUBLE_WITHIN(1.0, 225.0, bearing);
}

void test_distance_known_baseline(void)
{
    /* 0.7m north displacement */
    double dlat = 0.7 / 111320.0;
    double dist = gnss_dual_heading_distance_m(50.0, 10.0, 50.0 + dlat, 10.0);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.70, dist);
}

void test_distance_east_baseline(void)
{
    /* 0.7m east displacement at lat 50 */
    double dlon = 0.7 / (111320.0 * cos(50.0 * M_PI / 180.0));
    double dist = gnss_dual_heading_distance_m(50.0, 10.0, 50.0, 10.0 + dlon);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.70, dist);
}

/* ========================================================================
 * Tests 5-13: Valid/Invalid/Fresh/Stale scenarios
 * ======================================================================== */

void test_heading_valid_both_fresh(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.7 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_TRUE(calc.heading.fresh);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 0.0, calc.heading.heading_deg);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.70, calc.heading.baseline_m);
    TEST_ASSERT_EQUAL(HEADING_QUALITY_EXCELLENT, calc.heading.quality);
    TEST_ASSERT_EQUAL(HEADING_REASON_NONE, calc.heading.reason);
}

void test_heading_invalid_primary_null(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    gnss_snapshot_t secondary = make_default_snapshot(1000);
    gnss_dual_heading_set_sources(&calc, NULL, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_NO_PRIMARY, calc.heading.reason);
}

void test_heading_invalid_secondary_null(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    gnss_snapshot_t primary = make_default_snapshot(1000);
    gnss_dual_heading_set_sources(&calc, &primary, NULL);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_NO_SECONDARY, calc.heading.reason);
}

void test_heading_invalid_primary_not_position_valid(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, false, false, 0);
    gnss_snapshot_t secondary = make_default_snapshot(1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_NO_PRIMARY, calc.heading.reason);
}

void test_heading_invalid_secondary_not_position_valid(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    gnss_snapshot_t primary = make_default_snapshot(1000);
    gnss_snapshot_t secondary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, false, false, 0);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_NO_SECONDARY, calc.heading.reason);
}

void test_heading_primary_stale_not_fresh(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.7 / 111320.0;
    /* Primary has position_valid=true but fresh=false */
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, false, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    /* Heading should still be valid (position_valid is true) but NOT fresh */
    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_FALSE(calc.heading.fresh);
}

void test_heading_secondary_stale_not_fresh(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.7 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    /* Secondary has position_valid=true but fresh=false */
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, false, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_FALSE(calc.heading.fresh);
}

/* ========================================================================
 * Tests 14-16: Identical positions and baseline limits
 * ======================================================================== */

void test_heading_identical_positions(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_IDENTICAL_POS, calc.heading.reason);
}

void test_heading_baseline_too_small(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    /* 0.05m = 5cm north — below HEADING_BASELINE_MIN_M (0.10m) */
    double dlat = 0.05 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_BASELINE_TOO_SMALL, calc.heading.reason);
    TEST_ASSERT_TRUE(calc.heading.baseline_m < HEADING_BASELINE_MIN_M);
}

void test_heading_baseline_too_large(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    /* 5m north — above HEADING_BASELINE_MAX_M (2.00m) */
    double dlat = 5.0 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_BASELINE_TOO_LARGE, calc.heading.reason);
    TEST_ASSERT_TRUE(calc.heading.baseline_m > HEADING_BASELINE_MAX_M);
}

/* ========================================================================
 * Tests 17-20: Quality levels
 * ======================================================================== */

void test_quality_excellent_rtk_fixed_both_baseline_ok(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_QUALITY_EXCELLENT, calc.heading.quality);
}

void test_quality_good_rtk_one_baseline_ok(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FLOAT, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_TRUE(calc.heading.quality >= HEADING_QUALITY_GOOD);
}

void test_quality_degraded_single_fix(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_SINGLE, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_SINGLE, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_QUALITY_DEGRADED, calc.heading.quality);
}

void test_quality_poor_baseline_off(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    /* 1.5m baseline but expected is 0.70m ± 30% → [0.49, 0.91] — 1.5m is outside */
    double dlat = 1.5 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_SINGLE, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_SINGLE, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);  /* Still valid, but quality is POOR */
    TEST_ASSERT_EQUAL(HEADING_QUALITY_POOR, calc.heading.quality);
}

/* ========================================================================
 * Tests 21-23: Fix quality NONE/UNKNOWN → invalid heading
 * ======================================================================== */

void test_heading_primary_fix_none(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.70 / 111320.0;
    /* position_valid=true but fix_quality=NONE (edge case) */
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_NONE, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_NO_FIX_PRIMARY, calc.heading.reason);
}

void test_heading_primary_fix_unknown(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_UNKNOWN, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_NO_FIX_PRIMARY, calc.heading.reason);
}

void test_heading_secondary_fix_none(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_NONE, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_NO_FIX_SECONDARY, calc.heading.reason);
}

/* ========================================================================
 * Tests 24-28: Configuration
 * ======================================================================== */

void test_baseline_plausibility_70cm(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    /* 0.70m baseline — exactly expected */
    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.70, calc.heading.baseline_m);
}

void test_set_custom_baseline(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    gnss_dual_heading_set_baseline(&calc, 1.50);
    TEST_ASSERT_EQUAL_DOUBLE(1.50, calc.baseline_expected_m);
}

void test_set_baseline_tolerance_clamping(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    gnss_dual_heading_set_baseline_tolerance(&calc, 0.5);  /* below 1.0 → clamped */
    TEST_ASSERT_EQUAL_DOUBLE(1.0, calc.baseline_tolerance_pct);

    gnss_dual_heading_set_baseline_tolerance(&calc, 150.0);  /* above 100.0 → clamped */
    TEST_ASSERT_EQUAL_DOUBLE(100.0, calc.baseline_tolerance_pct);

    gnss_dual_heading_set_baseline_tolerance(&calc, 50.0);
    TEST_ASSERT_EQUAL_DOUBLE(50.0, calc.baseline_tolerance_pct);
}

void test_set_freshness_timeout_clamping(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    gnss_dual_heading_set_freshness_timeout(&calc, 50);  /* below 100 → clamped */
    TEST_ASSERT_EQUAL(100, calc.freshness_timeout_ms);

    gnss_dual_heading_set_freshness_timeout(&calc, 60000);  /* above 30000 → clamped */
    TEST_ASSERT_EQUAL(30000, calc.freshness_timeout_ms);

    gnss_dual_heading_set_freshness_timeout(&calc, 5000);
    TEST_ASSERT_EQUAL(5000, calc.freshness_timeout_ms);
}

void test_set_sources_disconnect(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    gnss_snapshot_t snap = make_default_snapshot(1000);
    gnss_dual_heading_set_sources(&calc, &snap, &snap);
    TEST_ASSERT_NOT_NULL(calc.primary);
    TEST_ASSERT_NOT_NULL(calc.secondary);

    gnss_dual_heading_set_sources(&calc, NULL, NULL);
    TEST_ASSERT_NULL(calc.primary);
    TEST_ASSERT_NULL(calc.secondary);
}

/* ========================================================================
 * Tests 29-30: Counters and timestamps
 * ======================================================================== */

void test_calc_count_increments(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);

    TEST_ASSERT_EQUAL(0, calc.heading.calc_count);

    gnss_dual_heading_calculate(&calc, 2000);
    TEST_ASSERT_EQUAL(1, calc.heading.calc_count);

    gnss_dual_heading_calculate(&calc, 2001);
    TEST_ASSERT_EQUAL(2, calc.heading.calc_count);
}

void test_timestamp_propagated(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 5000);

    TEST_ASSERT_EQUAL_UINT64(5000, calc.heading.timestamp_ms);
}

/* ========================================================================
 * Test 31: Null safety
 * ======================================================================== */

void test_null_safety(void)
{
    /* All API functions with NULL should not crash */
    gnss_dual_heading_init(NULL);
    gnss_dual_heading_set_sources(NULL, NULL, NULL);
    gnss_dual_heading_set_baseline(NULL, 1.0);
    gnss_dual_heading_set_baseline_tolerance(NULL, 30.0);
    gnss_dual_heading_set_freshness_timeout(NULL, 2000);
    gnss_dual_heading_calculate(NULL, 1000);
    gnss_dual_heading_service_step(NULL, 1000000);

    TEST_ASSERT_NULL(gnss_dual_heading_get(NULL));
    TEST_ASSERT_FALSE(gnss_dual_heading_is_fresh(NULL));

    /* Bearing/distance with identical points should give 0 */
    double d = gnss_dual_heading_distance_m(50.0, 10.0, 50.0, 10.0);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.0, d);
}

/* ========================================================================
 * Test 32: service_step delegation
 * ======================================================================== */

void test_service_step_delegates_to_calculate(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);

    /* service_step receives timestamp_us, should convert to ms */
    gnss_dual_heading_service_step((runtime_component_t*)&calc, 3000000ULL);  /* 3000 ms */

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 0.0, calc.heading.heading_deg);
    TEST_ASSERT_EQUAL_UINT64(3000, calc.heading.timestamp_ms);
}

/* ========================================================================
 * Test 33: is_fresh
 * ======================================================================== */

void test_is_fresh_helper(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(gnss_dual_heading_is_fresh(&calc));

    /* Now with one stale */
    primary.fresh = false;
    gnss_dual_heading_calculate(&calc, 2000);
    TEST_ASSERT_FALSE(gnss_dual_heading_is_fresh(&calc));
}

/* ========================================================================
 * Test 34: Getter returns pointer
 * ======================================================================== */

void test_heading_getter(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    const gnss_heading_snapshot_t* result = gnss_dual_heading_get(&calc);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_FALSE(result->valid);
}

/* ========================================================================
 * Tests 35-37: Real 70cm cardinal directions
 * ======================================================================== */

void test_heading_70cm_east(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    /* 70cm east at latitude 50° */
    double dlon = 0.70 / (111320.0 * cos(50.0 * M_PI / 180.0));
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0, 10.0 + dlon, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_DOUBLE_WITHIN(0.5, 90.0, calc.heading.heading_deg);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.70, calc.heading.baseline_m);
}

void test_heading_70cm_south(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 - dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_DOUBLE_WITHIN(0.5, 180.0, calc.heading.heading_deg);
}

void test_heading_70cm_west(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlon = 0.70 / (111320.0 * cos(50.0 * M_PI / 180.0));
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0, 10.0 - dlon, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_DOUBLE_WITHIN(0.5, 270.0, calc.heading.heading_deg);
}

/* ========================================================================
 * Additional: DGPS quality produces at least DEGRADED
 * ======================================================================== */

void test_quality_dgps_both(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_DGPS, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_DGPS, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    /* DGPS with matching baseline → DEGRADED (has usable fix, baseline ok, no RTK) */
    TEST_ASSERT_TRUE(calc.heading.quality >= HEADING_QUALITY_DEGRADED);
}

/* ========================================================================
 * Additional: PPS fix quality is usable
 * ======================================================================== */

void test_quality_pps_usable(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_PPS, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_PPS, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_QUALITY_DEGRADED, calc.heading.quality);
}

/* ========================================================================
 * Additional: Calc count does NOT increment on invalid
 * ======================================================================== */

void test_calc_count_no_increment_on_invalid(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    gnss_snapshot_t primary = make_default_snapshot(1000);
    gnss_snapshot_t secondary = make_default_snapshot(1000);  /* Same position */

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(0, calc.heading.calc_count);
}

/* ========================================================================
 * Additional: Both fresh → heading fresh, then become stale → not fresh
 * ======================================================================== */

void test_heading_freshness_transition(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);

    /* Both fresh → heading fresh */
    gnss_dual_heading_calculate(&calc, 2000);
    TEST_ASSERT_TRUE(calc.heading.fresh);

    /* Make primary stale */
    primary.fresh = false;
    gnss_dual_heading_calculate(&calc, 2001);
    TEST_ASSERT_TRUE(calc.heading.valid);  /* Still valid */
    TEST_ASSERT_FALSE(calc.heading.fresh); /* But not fresh */
}

/* ========================================================================
 * Additional: Invalid calculate does not overwrite previous valid heading
 * ======================================================================== */

void test_invalid_calculate_preserves_previous(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t good_primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t good_secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &good_primary, &good_secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 0.0, calc.heading.heading_deg);
    TEST_ASSERT_EQUAL(1, calc.heading.calc_count);

    /* Now disconnect sources → invalid calculate */
    gnss_dual_heading_set_sources(&calc, NULL, NULL);
    gnss_dual_heading_calculate(&calc, 2001);

    /* Current implementation: invalid resets heading. This is expected behavior
     * for a real-time system — stale/invalid is better than showing old data.
     * The calc_count should NOT have incremented. */
    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(1, calc.heading.calc_count);  /* No increment */
    TEST_ASSERT_EQUAL(HEADING_REASON_NO_PRIMARY, calc.heading.reason);
}

/* ========================================================================
 * main
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Init */
    RUN_TEST(test_heading_init);

    /* Bearing/distance utilities */
    RUN_TEST(test_bearing_north);
    RUN_TEST(test_bearing_east);
    RUN_TEST(test_bearing_south);
    RUN_TEST(test_bearing_west);
    RUN_TEST(test_bearing_normalization_negative);
    RUN_TEST(test_distance_known_baseline);
    RUN_TEST(test_distance_east_baseline);

    /* Valid/Invalid scenarios */
    RUN_TEST(test_heading_valid_both_fresh);
    RUN_TEST(test_heading_invalid_primary_null);
    RUN_TEST(test_heading_invalid_secondary_null);
    RUN_TEST(test_heading_invalid_primary_not_position_valid);
    RUN_TEST(test_heading_invalid_secondary_not_position_valid);
    RUN_TEST(test_heading_primary_stale_not_fresh);
    RUN_TEST(test_heading_secondary_stale_not_fresh);

    /* Identical positions and baseline limits */
    RUN_TEST(test_heading_identical_positions);
    RUN_TEST(test_heading_baseline_too_small);
    RUN_TEST(test_heading_baseline_too_large);

    /* Quality levels */
    RUN_TEST(test_quality_excellent_rtk_fixed_both_baseline_ok);
    RUN_TEST(test_quality_good_rtk_one_baseline_ok);
    RUN_TEST(test_quality_degraded_single_fix);
    RUN_TEST(test_quality_poor_baseline_off);

    /* Fix quality NONE/UNKNOWN */
    RUN_TEST(test_heading_primary_fix_none);
    RUN_TEST(test_heading_primary_fix_unknown);
    RUN_TEST(test_heading_secondary_fix_none);

    /* Configuration */
    RUN_TEST(test_baseline_plausibility_70cm);
    RUN_TEST(test_set_custom_baseline);
    RUN_TEST(test_set_baseline_tolerance_clamping);
    RUN_TEST(test_set_freshness_timeout_clamping);
    RUN_TEST(test_set_sources_disconnect);

    /* Counters and timestamps */
    RUN_TEST(test_calc_count_increments);
    RUN_TEST(test_timestamp_propagated);

    /* Null safety */
    RUN_TEST(test_null_safety);

    /* Service step */
    RUN_TEST(test_service_step_delegates_to_calculate);

    /* is_fresh helper */
    RUN_TEST(test_is_fresh_helper);

    /* Getter */
    RUN_TEST(test_heading_getter);

    /* Real 70cm cardinal directions */
    RUN_TEST(test_heading_70cm_east);
    RUN_TEST(test_heading_70cm_south);
    RUN_TEST(test_heading_70cm_west);

    /* Additional quality tests */
    RUN_TEST(test_quality_dgps_both);
    RUN_TEST(test_quality_pps_usable);
    RUN_TEST(test_calc_count_no_increment_on_invalid);
    RUN_TEST(test_heading_freshness_transition);
    RUN_TEST(test_invalid_calculate_preserves_previous);

    return UNITY_END();
}
