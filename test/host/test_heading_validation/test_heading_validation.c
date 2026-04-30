/* ========================================================================
 * test_heading_validation — Host tests for NAV-HEADING-001 NACHARBEIT
 *
 * Dual-Antenna Heading from Primary/Secondary GNSS Snapshots.
 *
 * CRITICAL BEHAVIOR CHANGE from v1 (Pflicht 3 — Strict Freshness):
 *   v1: stale snapshot → valid=true, fresh=false  (WRONG)
 *   Nacharbeit: stale snapshot → valid=false, reason=STALE  (CORRECT)
 *
 * Tests cover:
 *  1.  Init: all fields zero/invalid, mounting_offset=0.0
 *  2.  Bearing calculation: known North, East, South, West headings
 *  3.  0..360 normalization (negative angle, wraparound)
 *  4.  Distance calculation: known baseline
 *  5.  Valid heading: both snapshots position_valid AND fresh
 *  6.  NULL primary → heading invalid (PRIMARY_INVALID)
 *  7.  NULL secondary → heading invalid (SECONDARY_INVALID)
 *  8.  Primary not position_valid → heading invalid (PRIMARY_INVALID)
 *  9.  Secondary not position_valid → heading invalid (SECONDARY_INVALID)
 * 10.  Primary stale (position_valid but !fresh) → valid=false (PRIMARY_STALE)
 * 11.  Secondary stale → valid=false (SECONDARY_STALE)
 * 12.  Both stale → valid=false (PRIMARY_STALE, primary checked first)
 * 13.  Identical positions → heading invalid (IDENTICAL_POS)
 * 14.  Baseline near-zero → heading invalid (BASELINE_INVALID)
 * 15.  Baseline too large → heading invalid (BASELINE_INVALID)
 * 16.  Baseline exactly 0.70m → GOOD quality
 * 17.  Baseline 0.66m (GOOD range lower) → GOOD quality
 * 18.  Baseline 0.74m (GOOD range upper) → GOOD quality
 * 19.  Baseline 0.50m (DEGRADED boundary) → DEGRADED quality
 * 20.  Baseline 0.90m (DEGRADED boundary) → DEGRADED quality
 * 21.  Baseline 0.40m (BAD range) → BAD quality
 * 22.  Baseline 1.50m (BAD range) → BAD quality
 * 23.  Quality EXCELLENT: RTK_FIXED both + GOOD baseline + synced timestamps
 * 24.  Quality GOOD: RTK one + GOOD baseline
 * 25.  Quality DEGRADED: SINGLE fix + DEGRADED baseline
 * 26.  Quality BAD: timestamp delta > 100ms (desynced)
 * 27.  Timestamp mismatch > 250ms → valid=false (TIMESTAMP_MISMATCH)
 * 28.  Timestamp delta exactly at GOOD threshold (100ms) → synced
 * 29.  Timestamp delta exactly at MAX threshold (250ms) → desynced but valid
 * 30.  Swapped antennas → ~180° offset
 * 31.  359° ↔ 0° wraparound: heading near 0 stays near 0
 * 32.  Fix quality NONE on primary → invalid (NO_FIX_PRIMARY)
 * 33.  Fix quality UNKNOWN on primary → invalid (NO_FIX_PRIMARY)
 * 34.  Calc count increments only on valid calculations
 * 35.  Timestamp propagated correctly
 * 36.  Null safety: all API functions with NULL
 * 37.  service_step: delegates to calculate
 * 38.  is_fresh: valid + fresh only
 * 39.  Getter returns pointer
 * 40.  Real 70cm N/E/S/W cardinal directions
 * 41.  Baseline quality classification utility
 * 42.  Init includes timestamp_delta_ms=0, baseline_quality=INVALID
 * 43.  Both fresh → heading fresh, then one stale → valid=false
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

/* Create a GNSS snapshot with full control over all validity flags. */
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
    snap.motion_valid = is_fresh;  /* typically coupled with fresh for our tests */
    snap.valid = pos_valid && is_fresh;
    snap.fresh = is_fresh;
    snap.timestamp_ms = timestamp_ms;
    snap.last_gga_time_ms = timestamp_ms;
    snap.last_rmc_time_ms = timestamp_ms;
    return snap;
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
    TEST_ASSERT_EQUAL(HEADING_FRESHNESS_TIMEOUT_MS, calc.freshness_timeout_ms);
    TEST_ASSERT_NULL(calc.primary);
    TEST_ASSERT_NULL(calc.secondary);
    /* Pflicht 7: new fields */
    TEST_ASSERT_EQUAL_UINT64(0, calc.heading.timestamp_delta_ms);
    TEST_ASSERT_EQUAL(BASELINE_QUALITY_INVALID, calc.heading.baseline_quality);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, calc.heading.mounting_offset_deg);
}

/* ========================================================================
 * Tests 2-4: Bearing and Distance utilities
 * ======================================================================== */

void test_bearing_north(void)
{
    double dlat = 0.7 / 111320.0;
    double bearing = gnss_dual_heading_bearing(50.0, 10.0, 50.0 + dlat, 10.0);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 0.0, bearing);
}

void test_bearing_east(void)
{
    double dlon = 0.7 / (111320.0 * cos(50.0 * M_PI / 180.0));
    double bearing = gnss_dual_heading_bearing(50.0, 10.0, 50.0, 10.0 + dlon);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 90.0, bearing);
}

void test_bearing_south(void)
{
    double dlat = 0.7 / 111320.0;
    double bearing = gnss_dual_heading_bearing(50.0, 10.0, 50.0 - dlat, 10.0);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 180.0, bearing);
}

void test_bearing_west(void)
{
    double dlon = 0.7 / (111320.0 * cos(50.0 * M_PI / 180.0));
    double bearing = gnss_dual_heading_bearing(50.0, 10.0, 50.0, 10.0 - dlon);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 270.0, bearing);
}

void test_bearing_normalization_sw(void)
{
    /* Going SW: atan2(-1, -1) = -135° + 360 = 225° */
    double dlat = -0.7 / 111320.0;
    double dlon = -0.7 / (111320.0 * cos(50.0 * M_PI / 180.0));
    double bearing = gnss_dual_heading_bearing(50.0, 10.0, 50.0 + dlat, 10.0 + dlon);
    TEST_ASSERT_DOUBLE_WITHIN(1.0, 225.0, bearing);
}

void test_distance_known_baseline(void)
{
    double dlat = 0.7 / 111320.0;
    double dist = gnss_dual_heading_distance_m(50.0, 10.0, 50.0 + dlat, 10.0);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.70, dist);
}

/* ========================================================================
 * Tests 5-12: Valid/Invalid/Fresh/Stale (Pflicht 3 — STRICT)
 * ======================================================================== */

void test_heading_valid_both_position_valid_and_fresh(void)
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
    TEST_ASSERT_EQUAL(HEADING_REASON_NONE, calc.heading.reason);
}

void test_heading_invalid_primary_null(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    gnss_snapshot_t secondary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_dual_heading_set_sources(&calc, NULL, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_PRIMARY_INVALID, calc.heading.reason);
}

void test_heading_invalid_secondary_null(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_dual_heading_set_sources(&calc, &primary, NULL);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_SECONDARY_INVALID, calc.heading.reason);
}

void test_heading_invalid_primary_not_position_valid(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, false, false, 0);
    gnss_snapshot_t secondary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_PRIMARY_INVALID, calc.heading.reason);
}

void test_heading_invalid_secondary_not_position_valid(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, false, false, 0);
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_SECONDARY_INVALID, calc.heading.reason);
}

/* Pflicht 3: STRICT — stale primary → valid=false (NOT valid=true like in v1) */
void test_heading_strict_primary_stale_invalid(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.7 / 111320.0;
    /* position_valid=true but fresh=false → STALE */
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, false, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);   /* STRICT: invalid when stale */
    TEST_ASSERT_FALSE(calc.heading.fresh);
    TEST_ASSERT_EQUAL(HEADING_REASON_PRIMARY_STALE, calc.heading.reason);
}

/* Pflicht 3: STRICT — stale secondary → valid=false */
void test_heading_strict_secondary_stale_invalid(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.7 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, false, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_FALSE(calc.heading.fresh);
    TEST_ASSERT_EQUAL(HEADING_REASON_SECONDARY_STALE, calc.heading.reason);
}

/* Both stale → PRIMARY_STALE (primary checked first) */
void test_heading_both_stale_primary_stale_first(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 0.7 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, false, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, false, 1000);

    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_PRIMARY_STALE, calc.heading.reason);
}

/* ========================================================================
 * Tests 13-15: Identical positions and baseline limits
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

/* Pflicht 6: baseline near-zero → BASELINE_INVALID */
void test_heading_baseline_near_zero(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    /* 0.05m north — below MIN 0.10m */
    double dlat = 0.05 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_BASELINE_INVALID, calc.heading.reason);
    TEST_ASSERT_EQUAL(BASELINE_QUALITY_INVALID, calc.heading.baseline_quality);
}

void test_heading_baseline_too_large(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);

    double dlat = 5.0 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_BASELINE_INVALID, calc.heading.reason);
    TEST_ASSERT_EQUAL(BASELINE_QUALITY_INVALID, calc.heading.baseline_quality);
}

/* ========================================================================
 * Tests 16-22: Baseline quality tiers (Pflicht 1 — explicit ranges)
 * ======================================================================== */

/* Pflicht 6: baseline exactly 0.70m → GOOD */
void test_baseline_exact_70cm_good(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.70, calc.heading.baseline_m);
    TEST_ASSERT_EQUAL(BASELINE_QUALITY_GOOD, calc.heading.baseline_quality);
}

/* Pflicht 6: baseline slightly deviating — lower GOOD boundary */
void test_baseline_065m_good_boundary(void)
{
    TEST_ASSERT_EQUAL(BASELINE_QUALITY_GOOD,
        gnss_dual_heading_classify_baseline(0.65, 0.70));
}

/* Pflicht 6: baseline slightly deviating — upper GOOD boundary */
void test_baseline_075m_good_boundary(void)
{
    TEST_ASSERT_EQUAL(BASELINE_QUALITY_GOOD,
        gnss_dual_heading_classify_baseline(0.75, 0.70));
}

/* Pflicht 6: baseline at DEGRADED lower boundary */
void test_baseline_050m_degraded_boundary(void)
{
    TEST_ASSERT_EQUAL(BASELINE_QUALITY_DEGRADED,
        gnss_dual_heading_classify_baseline(0.50, 0.70));
}

/* Pflicht 6: baseline at DEGRADED upper boundary */
void test_baseline_090m_degraded_boundary(void)
{
    TEST_ASSERT_EQUAL(BASELINE_QUALITY_DEGRADED,
        gnss_dual_heading_classify_baseline(0.90, 0.70));
}

/* Pflicht 6: baseline strongly deviating — BAD */
void test_baseline_040m_bad(void)
{
    TEST_ASSERT_EQUAL(BASELINE_QUALITY_BAD,
        gnss_dual_heading_classify_baseline(0.40, 0.70));
}

/* Pflicht 6: baseline strongly deviating — BAD (high end) */
void test_baseline_150m_bad(void)
{
    TEST_ASSERT_EQUAL(BASELINE_QUALITY_BAD,
        gnss_dual_heading_classify_baseline(1.50, 0.70));
}

/* Pflicht 6: baseline below minimum → INVALID */
void test_baseline_classification_below_min(void)
{
    TEST_ASSERT_EQUAL(BASELINE_QUALITY_INVALID,
        gnss_dual_heading_classify_baseline(0.09, 0.70));
}

/* Pflicht 6: baseline above maximum → INVALID */
void test_baseline_classification_above_max(void)
{
    TEST_ASSERT_EQUAL(BASELINE_QUALITY_INVALID,
        gnss_dual_heading_classify_baseline(2.01, 0.70));
}

/* Baseline DEGRADED with SINGLE fix → quality DEGRADED */
void test_quality_degraded_baseline_degraded_single(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlat = 0.50 / 111320.0;  /* 50cm — DEGRADED range */
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_SINGLE, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_SINGLE, true, true, 1000);
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_EQUAL(BASELINE_QUALITY_DEGRADED, calc.heading.baseline_quality);
    TEST_ASSERT_EQUAL(HEADING_QUALITY_DEGRADED, calc.heading.quality);
}

/* Baseline BAD → quality BAD even with RTK */
void test_quality_bad_baseline_even_with_rtk(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlat = 1.50 / 111320.0;  /* 1.5m — BAD range */
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_EQUAL(BASELINE_QUALITY_BAD, calc.heading.baseline_quality);
    TEST_ASSERT_EQUAL(HEADING_QUALITY_BAD, calc.heading.quality);
}

/* Quality EXCELLENT: RTK_FIXED both + GOOD baseline + synced timestamps */
void test_quality_excellent_all_conditions(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1001);  /* 1ms delta */
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_QUALITY_EXCELLENT, calc.heading.quality);
    TEST_ASSERT_EQUAL(BASELINE_QUALITY_GOOD, calc.heading.baseline_quality);
    TEST_ASSERT_TRUE(calc.heading.timestamp_delta_ms <= 100);
}

/* Quality GOOD: RTK one + GOOD baseline */
void test_quality_good_rtk_one_good_baseline(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FLOAT, true, true, 1000);
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_QUALITY_GOOD, calc.heading.quality);
}

/* ========================================================================
 * Tests 23-29: Timestamp Synchronicity (Pflicht 2)
 * ======================================================================== */

/* Pflicht 6: timestamp mismatch → quality BAD (delta 150ms, between 100-250) */
void test_timestamp_desync_quality_bad(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1150);  /* 150ms delta */
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);  /* Still valid (<= 250ms) */
    TEST_ASSERT_EQUAL(HEADING_QUALITY_BAD, calc.heading.quality);  /* But BAD */
    TEST_ASSERT_EQUAL_UINT64(150, calc.heading.timestamp_delta_ms);
}

/* Pflicht 6: timestamp mismatch > 250ms → valid=false */
void test_timestamp_mismatch_invalid(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1300);  /* 300ms delta */
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_REASON_TIMESTAMP_MISMATCH, calc.heading.reason);
    TEST_ASSERT_EQUAL_UINT64(300, calc.heading.timestamp_delta_ms);
}

/* Timestamp delta exactly at GOOD threshold (100ms) → synced, no penalty */
void test_timestamp_delta_at_good_threshold(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1100);  /* exactly 100ms */
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_EQUAL(HEADING_QUALITY_EXCELLENT, calc.heading.quality);
    TEST_ASSERT_EQUAL_UINT64(100, calc.heading.timestamp_delta_ms);
}

/* Timestamp delta exactly at MAX threshold (250ms) → desynced but valid */
void test_timestamp_delta_at_max_threshold(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1250);  /* exactly 250ms */
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);  /* Still valid (exactly at limit) */
    TEST_ASSERT_EQUAL(HEADING_QUALITY_BAD, calc.heading.quality);  /* Quality BAD */
    TEST_ASSERT_EQUAL_UINT64(250, calc.heading.timestamp_delta_ms);
}

/* Timestamp delta 0ms → perfect sync */
void test_timestamp_delta_zero_perfect(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);  /* same ts */
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_EQUAL_UINT64(0, calc.heading.timestamp_delta_ms);
}

/* ========================================================================
 * Test 30: Swapped antennas (180° offset)
 * ======================================================================== */

/* Pflicht 6: swapped antennas → heading + 180° */
void test_swapped_antennas_180_offset(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlat = 0.70 / 111320.0;

    /* Normal: primary at south, secondary at north → heading ~0° (north) */
    gnss_snapshot_t primary_south = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary_north = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_dual_heading_set_sources(&calc, &primary_south, &secondary_north);
    gnss_dual_heading_calculate(&calc, 2000);
    double heading_normal = calc.heading.heading_deg;

    /* Swapped: primary at north, secondary at south → heading ~180° (south) */
    gnss_dual_heading_calc_t calc2;
    gnss_dual_heading_init(&calc2);
    gnss_dual_heading_set_sources(&calc2, &secondary_north, &primary_south);
    gnss_dual_heading_calculate(&calc2, 2000);
    double heading_swapped = calc2.heading.heading_deg;

    /* The two headings should differ by ~180° */
    double diff = fabs(heading_swapped - heading_normal);
    if (diff > 180.0) diff = 360.0 - diff;  /* Handle wraparound */
    TEST_ASSERT_DOUBLE_WITHIN(1.0, 180.0, diff);
}

/* ========================================================================
 * Test 31: 359° ↔ 0° wraparound
 * ======================================================================== */

/* Pflicht 6: heading near 0° stays near 0°, near 360° wraps correctly */
void test_heading_359_0_wraparound(void)
{
    /* Heading just west of north should be near 360° (or 0°) */
    double tiny_west = 0.01 / (111320.0 * cos(50.0 * M_PI / 180.0));
    double bearing = gnss_dual_heading_bearing(50.0, 10.0, 50.0, 10.0 - tiny_west);
    /* Should be close to 360° (or equivalently 0°) */
    TEST_ASSERT_TRUE(bearing > 359.0 || bearing < 1.0);

    /* Heading just east of north should be near 0° */
    double tiny_east = 0.01 / (111320.0 * cos(50.0 * M_PI / 180.0));
    double bearing_east = gnss_dual_heading_bearing(50.0, 10.0, 50.0, 10.0 + tiny_east);
    TEST_ASSERT_TRUE(bearing_east < 1.0);
}

/* ========================================================================
 * Tests 32-33: Fix quality NONE/UNKNOWN
 * ======================================================================== */

void test_heading_primary_fix_none(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlat = 0.70 / 111320.0;
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

/* ========================================================================
 * Tests 34-35: Counters and timestamps
 * ======================================================================== */

void test_calc_count_only_on_valid(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);  /* Same pos */
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);

    gnss_dual_heading_calculate(&calc, 2000);  /* Invalid (identical) */
    TEST_ASSERT_EQUAL(0, calc.heading.calc_count);

    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t good_sec = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_dual_heading_set_sources(&calc, &primary, &good_sec);
    gnss_dual_heading_calculate(&calc, 2000);  /* Valid */
    TEST_ASSERT_EQUAL(1, calc.heading.calc_count);

    gnss_dual_heading_calculate(&calc, 2001);  /* Valid again */
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
 * Test 36: Null safety
 * ======================================================================== */

void test_null_safety(void)
{
    gnss_dual_heading_init(NULL);
    gnss_dual_heading_set_sources(NULL, NULL, NULL);
    gnss_dual_heading_set_baseline(NULL, 1.0);
    gnss_dual_heading_set_freshness_timeout(NULL, 2000);
    gnss_dual_heading_calculate(NULL, 1000);
    gnss_dual_heading_service_step(NULL, 1000000);
    TEST_ASSERT_NULL(gnss_dual_heading_get(NULL));
    TEST_ASSERT_FALSE(gnss_dual_heading_is_fresh(NULL));

    double d = gnss_dual_heading_distance_m(50.0, 10.0, 50.0, 10.0);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.0, d);

    /* classify_baseline with valid inputs */
    TEST_ASSERT_EQUAL(BASELINE_QUALITY_INVALID, gnss_dual_heading_classify_baseline(0.0, 0.70));
}

/* ========================================================================
 * Tests 37-39: Service step, is_fresh, getter
 * ======================================================================== */

void test_service_step_delegates(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_service_step((runtime_component_t*)&calc, 3000000ULL);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_EQUAL_UINT64(3000, calc.heading.timestamp_ms);
}

void test_is_fresh_strict(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(gnss_dual_heading_is_fresh(&calc));

    /* Make primary stale → valid=false, so is_fresh=false */
    primary.fresh = false;
    gnss_dual_heading_calculate(&calc, 2000);
    TEST_ASSERT_FALSE(gnss_dual_heading_is_fresh(&calc));
    TEST_ASSERT_FALSE(calc.heading.valid);
}

void test_heading_getter(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    const gnss_heading_snapshot_t* result = gnss_dual_heading_get(&calc);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_FALSE(result->valid);
}

/* ========================================================================
 * Test 40: Real 70cm cardinal directions
 * ======================================================================== */

void test_heading_70cm_north(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_DOUBLE_WITHIN(0.5, 0.0, calc.heading.heading_deg);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.70, calc.heading.baseline_m);
}

void test_heading_70cm_east(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlon = 0.70 / (111320.0 * cos(50.0 * M_PI / 180.0));
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0, 10.0 + dlon, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);
    gnss_dual_heading_calculate(&calc, 2000);

    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_DOUBLE_WITHIN(0.5, 90.0, calc.heading.heading_deg);
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
 * Test 43: Freshness transition (both fresh → one stale → valid=false)
 * ======================================================================== */

void test_heading_freshness_strict_transition(void)
{
    gnss_dual_heading_calc_t calc;
    gnss_dual_heading_init(&calc);
    double dlat = 0.70 / 111320.0;
    gnss_snapshot_t primary = make_snapshot(50.0, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_snapshot_t secondary = make_snapshot(50.0 + dlat, 10.0, GNSS_FIX_RTK_FIXED, true, true, 1000);
    gnss_dual_heading_set_sources(&calc, &primary, &secondary);

    /* Both fresh → heading valid + fresh */
    gnss_dual_heading_calculate(&calc, 2000);
    TEST_ASSERT_TRUE(calc.heading.valid);
    TEST_ASSERT_TRUE(calc.heading.fresh);

    /* Make primary stale → valid=false (STRICT) */
    primary.fresh = false;
    gnss_dual_heading_calculate(&calc, 2001);
    TEST_ASSERT_FALSE(calc.heading.valid);
    TEST_ASSERT_FALSE(calc.heading.fresh);
    TEST_ASSERT_EQUAL(HEADING_REASON_PRIMARY_STALE, calc.heading.reason);
}

/* ========================================================================
 * main
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* 1. Init */
    RUN_TEST(test_heading_init);

    /* 2-4. Bearing/distance utilities */
    RUN_TEST(test_bearing_north);
    RUN_TEST(test_bearing_east);
    RUN_TEST(test_bearing_south);
    RUN_TEST(test_bearing_west);
    RUN_TEST(test_bearing_normalization_sw);
    RUN_TEST(test_distance_known_baseline);

    /* 5-12. Valid/Invalid/Fresh/Stale (STRICT — Pflicht 3) */
    RUN_TEST(test_heading_valid_both_position_valid_and_fresh);
    RUN_TEST(test_heading_invalid_primary_null);
    RUN_TEST(test_heading_invalid_secondary_null);
    RUN_TEST(test_heading_invalid_primary_not_position_valid);
    RUN_TEST(test_heading_invalid_secondary_not_position_valid);
    RUN_TEST(test_heading_strict_primary_stale_invalid);
    RUN_TEST(test_heading_strict_secondary_stale_invalid);
    RUN_TEST(test_heading_both_stale_primary_stale_first);

    /* 13-15. Identical positions and baseline limits */
    RUN_TEST(test_heading_identical_positions);
    RUN_TEST(test_heading_baseline_near_zero);
    RUN_TEST(test_heading_baseline_too_large);

    /* 16-22. Baseline quality tiers (Pflicht 1) */
    RUN_TEST(test_baseline_exact_70cm_good);
    RUN_TEST(test_baseline_065m_good_boundary);
    RUN_TEST(test_baseline_075m_good_boundary);
    RUN_TEST(test_baseline_050m_degraded_boundary);
    RUN_TEST(test_baseline_090m_degraded_boundary);
    RUN_TEST(test_baseline_040m_bad);
    RUN_TEST(test_baseline_150m_bad);
    RUN_TEST(test_baseline_classification_below_min);
    RUN_TEST(test_baseline_classification_above_max);
    RUN_TEST(test_quality_degraded_baseline_degraded_single);
    RUN_TEST(test_quality_bad_baseline_even_with_rtk);

    /* Quality levels */
    RUN_TEST(test_quality_excellent_all_conditions);
    RUN_TEST(test_quality_good_rtk_one_good_baseline);

    /* 23-29. Timestamp synchronicity (Pflicht 2) */
    RUN_TEST(test_timestamp_desync_quality_bad);
    RUN_TEST(test_timestamp_mismatch_invalid);
    RUN_TEST(test_timestamp_delta_at_good_threshold);
    RUN_TEST(test_timestamp_delta_at_max_threshold);
    RUN_TEST(test_timestamp_delta_zero_perfect);

    /* 30. Swapped antennas */
    RUN_TEST(test_swapped_antennas_180_offset);

    /* 31. 359° ↔ 0° wraparound */
    RUN_TEST(test_heading_359_0_wraparound);

    /* 32-33. Fix quality NONE/UNKNOWN */
    RUN_TEST(test_heading_primary_fix_none);
    RUN_TEST(test_heading_primary_fix_unknown);

    /* 34-35. Counters and timestamps */
    RUN_TEST(test_calc_count_only_on_valid);
    RUN_TEST(test_timestamp_propagated);

    /* 36. Null safety */
    RUN_TEST(test_null_safety);

    /* 37-39. Service step, is_fresh, getter */
    RUN_TEST(test_service_step_delegates);
    RUN_TEST(test_is_fresh_strict);
    RUN_TEST(test_heading_getter);

    /* 40. Real 70cm cardinal directions */
    RUN_TEST(test_heading_70cm_north);
    RUN_TEST(test_heading_70cm_east);
    RUN_TEST(test_heading_70cm_south);
    RUN_TEST(test_heading_70cm_west);

    /* 43. Freshness strict transition */
    RUN_TEST(test_heading_freshness_strict_transition);

    return UNITY_END();
}
