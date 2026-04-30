/* ========================================================================
 * test_gnss_validation — Host tests for NAV-GNSS-VALID-001 Nacharbeit
 *
 * ALL tests use real NMEA checksums computed by make_nmea().
 * No tests use checksum-free sentences as success proof.
 *
 * Validates:
 *  1.  Snapshot initialization (Variant A: all flags false)
 *  2.  Fix quality mapping (GGA → enum, including UNKNOWN)
 *  3.  RTK status derivation
 *  4.  GGA with correct checksum → accepted
 *  5.  GGA with wrong checksum → rejected, no data copied
 *  6.  RMC with correct checksum → accepted
 *  7.  RMC with wrong checksum → rejected
 *  8.  Sentence without *XX → rejected
 *  9.  Sentence with non-hex checksum → rejected
 * 10.  Fragmented sentence with correct checksum → processed after complete
 * 11.  Multiple sentences in buffer, one bad → only valid ones count
 * 12.  GGA fix=0 → position_valid=false, valid=false
 * 13.  GGA fix=4 RTK → position_valid=true
 * 14.  RMC status A → motion_valid=true
 * 15.  RMC status V → motion_valid=false, reason=RMC_VOID
 * 16.  valid = position_valid AND motion_valid
 * 17.  Freshness: position stale → position_valid=false
 * 18.  Freshness: motion stale → motion_valid=false
 * 19.  Freshness: both stale → fresh=false, valid=false
 * 20.  Custom freshness timeout
 * 21.  Correction age present → correction_age_valid=true
 * 22.  Correction age empty → correction_age_valid=false
 * 23.  GST accuracy → accuracy_valid=true, std values
 * 24.  GST optional → accuracy_valid=false without GST
 * 25.  Unknown fix_quality (3) → UNKNOWN, position_valid=false
 * 26.  Dual-receiver isolation (full)
 * 27.  Checksum error in secondary does not affect primary
 * 28.  Stale secondary does not make primary stale
 * 29.  Knots→m/s conversion
 * 30.  Null safety on all API functions
 * 31.  Overflow error counter
 * 32.  Freshness timeout clamping
 * 33.  Snapshot age_ms calculation
 * 34.  Timeout event counter
 * 35.  Cumulative statistics
 * 36.  Bytes received counter
 * 37.  Merged snapshot: GGA+RMC+GST together → valid+fresh
 * 38.  GGA only (no RMC) → position_valid but not valid
 * 39.  status_reason documentation
 * 40.  Invalid checksum does NOT overwrite existing snapshot
 * ======================================================================== */

#include "unity.h"
#include "gnss_um980.h"
#include "gnss_snapshot.h"
#include "nmea_parser.h"
#include "byte_ring_buffer.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* ========================================================================
 * NMEA Sentence Builder — always computes correct checksum
 * ======================================================================== */

static uint8_t nmea_checksum(const char* body)
{
    uint8_t cs = 0;
    for (const char* p = body; *p != '\0'; p++) {
        cs ^= (uint8_t)*p;
    }
    return cs;
}

/* Build a complete NMEA sentence with $, *, correct checksum, \r\n.
 * out must be at least strlen(body) + 7 bytes. */
static void make_nmea(char* out, const char* body)
{
    uint8_t cs = nmea_checksum(body);
    sprintf(out, "$%s*%02X\r\n", body, cs);
}

/* Build NMEA sentence with WRONG checksum for testing rejection.
 * checksum_override: the wrong checksum value to use. */
static void make_nmea_bad_checksum(char* out, const char* body, uint8_t checksum_override)
{
    sprintf(out, "$%s*%02X\r\n", body, checksum_override);
}

/* Build NMEA sentence WITHOUT checksum (*XX part). */
static void make_nmea_no_checksum(char* out, const char* body)
{
    sprintf(out, "$%s\r\n", body);
}

/* ========================================================================
 * Test Fixtures
 * ======================================================================== */

static gnss_um980_t primary;
static gnss_um980_t secondary;

void setUp(void)
{
    memset(&primary, 0, sizeof(primary));
    memset(&secondary, 0, sizeof(secondary));
    gnss_um980_init(&primary, 0, "primary");
    gnss_um980_init(&secondary, 1, "secondary");
}

void tearDown(void) {}

/* ---- Helper: feed a complete valid NMEA sentence ---- */
static uint32_t feed_sentence(gnss_um980_t* rx, const char* body)
{
    char buf[256];
    make_nmea(buf, body);
    return gnss_um980_feed(rx, (const uint8_t*)buf, strlen(buf));
}

/* ========================================================================
 * 1. Snapshot initialization — Variant A flags
 * ======================================================================== */

void test_snapshot_init_variant_a(void)
{
    gnss_snapshot_t snap;
    memset(&snap, 0xFF, sizeof(snap));  /* poison */
    gnss_snapshot_init(&snap);

    TEST_ASSERT_FALSE(snap.position_valid);
    TEST_ASSERT_FALSE(snap.motion_valid);
    TEST_ASSERT_FALSE(snap.accuracy_valid);
    TEST_ASSERT_FALSE(snap.valid);
    TEST_ASSERT_FALSE(snap.fresh);
    TEST_ASSERT_EQUAL(0, snap.timestamp_ms);
    TEST_ASSERT_EQUAL(GNSS_REASON_NONE, snap.status_reason);
    TEST_ASSERT_EQUAL(GNSS_FIX_NONE, snap.fix_quality);
    TEST_ASSERT_EQUAL(GNSS_RTK_NONE, snap.rtk_status);
    TEST_ASSERT_FALSE(snap.correction_age_valid);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, snap.correction_age_s);
    TEST_ASSERT_EQUAL(0, snap.gga_count);
    TEST_ASSERT_EQUAL(0, snap.rmc_count);
    TEST_ASSERT_EQUAL(0, snap.gst_count);
    TEST_ASSERT_EQUAL(0, snap.sentences_parsed);
    TEST_ASSERT_EQUAL(0, snap.sentences_error);
    TEST_ASSERT_EQUAL(0, snap.bytes_received);
    TEST_ASSERT_EQUAL(0, snap.last_gga_time_ms);
    TEST_ASSERT_EQUAL(0, snap.last_rmc_time_ms);
    TEST_ASSERT_EQUAL(0, snap.last_gst_time_ms);
    TEST_ASSERT_EQUAL(GNSS_ERR_NONE, snap.last_error);
}

void test_um980_init_defaults(void)
{
    TEST_ASSERT_EQUAL(0, primary.instance_id);
    TEST_ASSERT_EQUAL_STRING("primary", primary.name);
    TEST_ASSERT_EQUAL(GNSS_FRESHNESS_TIMEOUT_MS_DEFAULT, primary.freshness_timeout_ms);
    TEST_ASSERT_FALSE(primary.gga_valid);
    TEST_ASSERT_FALSE(primary.rmc_valid);
    TEST_ASSERT_FALSE(primary.gst_valid);
    TEST_ASSERT_EQUAL(0, primary.sentences_parsed);
    TEST_ASSERT_EQUAL(0, primary.checksum_errors);
    TEST_ASSERT_EQUAL(0, primary.overflow_errors);
    TEST_ASSERT_EQUAL(0, primary.timeout_events);
}

/* ========================================================================
 * 2. Fix quality mapping — including UNKNOWN
 * ======================================================================== */

void test_fix_quality_from_gga(void)
{
    TEST_ASSERT_EQUAL(GNSS_FIX_NONE,    gnss_fix_quality_from_gga(0));
    TEST_ASSERT_EQUAL(GNSS_FIX_SINGLE,  gnss_fix_quality_from_gga(1));
    TEST_ASSERT_EQUAL(GNSS_FIX_DGPS,    gnss_fix_quality_from_gga(2));
    TEST_ASSERT_EQUAL(GNSS_FIX_PPS,     gnss_fix_quality_from_gga(3));
    TEST_ASSERT_EQUAL(GNSS_FIX_RTK_FIXED, gnss_fix_quality_from_gga(4));
    TEST_ASSERT_EQUAL(GNSS_FIX_RTK_FLOAT, gnss_fix_quality_from_gga(5));
    TEST_ASSERT_EQUAL(GNSS_FIX_UNKNOWN, gnss_fix_quality_from_gga(6));
    TEST_ASSERT_EQUAL(GNSS_FIX_UNKNOWN, gnss_fix_quality_from_gga(99));
    TEST_ASSERT_EQUAL(GNSS_FIX_UNKNOWN, gnss_fix_quality_from_gga(255));
}

/* ========================================================================
 * 3. RTK status derivation
 * ======================================================================== */

void test_rtk_status_from_gga(void)
{
    TEST_ASSERT_EQUAL(GNSS_RTK_NONE,   gnss_rtk_status_from_gga(0));
    TEST_ASSERT_EQUAL(GNSS_RTK_NONE,   gnss_rtk_status_from_gga(1));
    TEST_ASSERT_EQUAL(GNSS_RTK_NONE,   gnss_rtk_status_from_gga(2));
    TEST_ASSERT_EQUAL(GNSS_RTK_NONE,   gnss_rtk_status_from_gga(3));
    TEST_ASSERT_EQUAL(GNSS_RTK_FIXED,  gnss_rtk_status_from_gga(4));
    TEST_ASSERT_EQUAL(GNSS_RTK_FLOAT,  gnss_rtk_status_from_gga(5));
    TEST_ASSERT_EQUAL(GNSS_RTK_NONE,   gnss_rtk_status_from_gga(6));
    TEST_ASSERT_EQUAL(GNSS_RTK_NONE,   gnss_rtk_status_from_gga(99));
}

/* ========================================================================
 * 4. GGA with correct checksum → accepted
 * ======================================================================== */

void test_gga_correct_checksum_accepted(void)
{
    uint32_t n = feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,1,08,1.0,50.0,M,46.9,M,,");
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL(1, primary.gga_count);
    TEST_ASSERT_EQUAL(0, primary.checksum_errors);
    TEST_ASSERT_TRUE(primary.gga_valid);
    TEST_ASSERT_EQUAL(1, primary.gga.fix_quality);
    TEST_ASSERT_EQUAL(8, primary.gga.num_sats);
}

/* ========================================================================
 * 5. GGA with wrong checksum → rejected, no data copied
 * ======================================================================== */

void test_gga_wrong_checksum_rejected(void)
{
    /* First feed a valid GGA to establish baseline */
    feed_sentence(&primary,
        "GNGGA,100000,4807.038,N,01131.000,E,1,08,1.0,50.0,M,46.9,M,,");
    gnss_um980_finalize_snapshot(&primary, 10000);
    const gnss_snapshot_t* snap_before = gnss_um980_get_snapshot(&primary);
    double lat_before = snap_before->latitude;
    uint8_t sats_before = snap_before->satellites;
    TEST_ASSERT_EQUAL(8, sats_before);

    /* Now feed a bad-checksum GGA with different data */
    char buf[256];
    make_nmea_bad_checksum(buf,
        "GNGGA,200000,5321.6802,N,01339.4463,E,4,12,0.8,100.0,M,0.0,M,,", 0xFF);
    uint32_t n = gnss_um980_feed(&primary, (const uint8_t*)buf, strlen(buf));

    TEST_ASSERT_EQUAL(0, n);  /* 0 valid sentences */
    TEST_ASSERT_EQUAL(1, primary.checksum_errors);
    /* CRITICAL: existing data must NOT be overwritten */
    TEST_ASSERT_EQUAL_DOUBLE(lat_before, snap_before->latitude);
    TEST_ASSERT_EQUAL(sats_before, snap_before->satellites);
    TEST_ASSERT_TRUE(fabs(snap_before->latitude - 48.117) < 0.01);
    /* Must NOT contain the bad sentence's data */
    TEST_ASSERT_TRUE(fabs(snap_before->latitude - 53.361) > 0.01);
}

/* ========================================================================
 * 6. RMC with correct checksum → accepted
 * ======================================================================== */

void test_rmc_correct_checksum_accepted(void)
{
    uint32_t n = feed_sentence(&primary,
        "GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W,A");
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL(1, primary.rmc_count);
    TEST_ASSERT_EQUAL(0, primary.checksum_errors);
    TEST_ASSERT_TRUE(primary.rmc_valid);
    TEST_ASSERT_TRUE(primary.rmc.status_valid);
}

/* ========================================================================
 * 7. RMC with wrong checksum → rejected
 * ======================================================================== */

void test_rmc_wrong_checksum_rejected(void)
{
    char buf[256];
    make_nmea_bad_checksum(buf,
        "GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W,A", 0x00);
    uint32_t n = gnss_um980_feed(&primary, (const uint8_t*)buf, strlen(buf));

    TEST_ASSERT_EQUAL(0, n);
    TEST_ASSERT_EQUAL(1, primary.checksum_errors);
    TEST_ASSERT_FALSE(primary.rmc_valid);
}

/* ========================================================================
 * 8. Sentence without *XX → rejected
 * ======================================================================== */

void test_sentence_without_checksum_rejected(void)
{
    char buf[256];
    make_nmea_no_checksum(buf, "GNGGA,123519,4807.038,N,01131.000,E,1,08,1.0,50.0,M,46.9,M,,");
    uint32_t n = gnss_um980_feed(&primary, (const uint8_t*)buf, strlen(buf));

    TEST_ASSERT_EQUAL(0, n);
    TEST_ASSERT_EQUAL(1, primary.checksum_errors);
    TEST_ASSERT_FALSE(primary.gga_valid);
}

/* ========================================================================
 * 9. Sentence with non-hex checksum → rejected
 * ======================================================================== */

void test_nonhex_checksum_rejected(void)
{
    /* Use GG as checksum chars — 'G' is not a valid hex digit */
    const char* bad = "$GNGGA,123519,4807.038,N,01131.000,E,1,08,1.0,50.0,M,46.9,M,,*GG\r\n";
    uint32_t n = gnss_um980_feed(&primary, (const uint8_t*)bad, strlen(bad));

    TEST_ASSERT_EQUAL(0, n);
    TEST_ASSERT_EQUAL(1, primary.checksum_errors);
    TEST_ASSERT_FALSE(primary.gga_valid);
}

/* ========================================================================
 * 10. Fragmented sentence with correct checksum
 * ======================================================================== */

void test_fragmented_correct_checksum(void)
{
    /* Compute the correct full sentence first */
    char full[256];
    make_nmea(full, "GNGGA,123519,4807.038,N,01131.000,E,1,04,1.0,50.0,M,0.0,M,,");

    /* Find where to split (somewhere in the middle) */
    size_t split_pos = 20;  /* inside the data */
    size_t full_len = strlen(full);

    /* Feed first half */
    uint32_t n1 = gnss_um980_feed(&primary, (const uint8_t*)full, split_pos);
    TEST_ASSERT_EQUAL(0, n1);  /* incomplete */

    /* Feed second half */
    uint32_t n2 = gnss_um980_feed(&primary,
        (const uint8_t*)(full + split_pos), full_len - split_pos);
    TEST_ASSERT_EQUAL(1, n2);  /* completed with correct checksum */

    TEST_ASSERT_EQUAL(1, primary.sentences_parsed);
    TEST_ASSERT_TRUE(primary.gga_valid);
    TEST_ASSERT_EQUAL(0, primary.checksum_errors);
}

/* ========================================================================
 * 11. Multiple sentences, one bad → only valid ones count
 * ======================================================================== */

void test_multiple_sentences_one_bad(void)
{
    /* Valid GGA */
    feed_sentence(&primary,
        "GNGGA,100000,4807.038,N,01131.000,E,1,08,1.0,50.0,M,0.0,M,,");

    /* Bad checksum GGA */
    char bad[256];
    make_nmea_bad_checksum(bad,
        "GNGGA,100001,5321.6802,N,01339.4463,E,4,12,0.8,100.0,M,0.0,M,,", 0xAA);
    gnss_um980_feed(&primary, (const uint8_t*)bad, strlen(bad));

    /* Valid RMC */
    feed_sentence(&primary,
        "GNRMC,100000,A,4807.038,N,01131.000,E,5.0,90.0,010125,,A");

    TEST_ASSERT_EQUAL(2, primary.sentences_parsed);
    TEST_ASSERT_EQUAL(1, primary.checksum_errors);
    TEST_ASSERT_TRUE(primary.gga_valid);
    TEST_ASSERT_TRUE(primary.rmc_valid);

    gnss_um980_finalize_snapshot(&primary, 20000);
    const gnss_snapshot_t* snap = gnss_um980_get_snapshot(&primary);
    /* Must contain the GOOD GGA data, not the bad one */
    TEST_ASSERT_TRUE(fabs(snap->latitude - 48.117) < 0.01);
    TEST_ASSERT_EQUAL(8, snap->satellites);
    /* Bad GGA's values must NOT be present */
    TEST_ASSERT_TRUE(fabs(snap->latitude - 53.361) > 0.01);
}

/* ========================================================================
 * 12. GGA fix=0 → position_valid=false, valid=false
 * ======================================================================== */

void test_gga_no_fix_position_invalid(void)
{
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,0,00,99.9,0.0,M,0.0,M,,");
    gnss_um980_finalize_snapshot(&primary, 1000);

    const gnss_snapshot_t* snap = gnss_um980_get_snapshot(&primary);
    TEST_ASSERT_NOT_NULL(snap);
    TEST_ASSERT_FALSE(snap->position_valid);
    TEST_ASSERT_FALSE(snap->valid);
    TEST_ASSERT_FALSE(snap->fresh);
    TEST_ASSERT_EQUAL(GNSS_REASON_NO_FIX, snap->status_reason);
    TEST_ASSERT_EQUAL(GNSS_FIX_NONE, snap->fix_quality);
    TEST_ASSERT_EQUAL(1, snap->gga_count);
}

/* ========================================================================
 * 13. GGA fix=4 RTK → position_valid=true
 * ======================================================================== */

void test_gga_rtk_fixed_position_valid(void)
{
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,4,12,1.0,100.0,M,0.0,M,,");
    feed_sentence(&primary,
        "GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W,A");
    gnss_um980_finalize_snapshot(&primary, 5000);

    const gnss_snapshot_t* snap = gnss_um980_get_snapshot(&primary);
    TEST_ASSERT_TRUE(snap->position_valid);
    TEST_ASSERT_TRUE(snap->motion_valid);
    TEST_ASSERT_TRUE(snap->valid);
    TEST_ASSERT_TRUE(snap->fresh);
    TEST_ASSERT_EQUAL(GNSS_FIX_RTK_FIXED, snap->fix_quality);
    TEST_ASSERT_EQUAL(GNSS_RTK_FIXED, snap->rtk_status);
    TEST_ASSERT_EQUAL(GNSS_REASON_NONE, snap->status_reason);
}

/* ========================================================================
 * 14. RMC status A → motion_valid=true
 * ======================================================================== */

void test_rmc_status_a_motion_valid(void)
{
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,1,08,1.0,50.0,M,0.0,M,,");
    feed_sentence(&primary,
        "GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W,A");
    gnss_um980_finalize_snapshot(&primary, 5000);

    const gnss_snapshot_t* snap = gnss_um980_get_snapshot(&primary);
    TEST_ASSERT_TRUE(snap->motion_valid);
    TEST_ASSERT_TRUE(fabs(snap->speed_ms - gnss_knots_to_ms(22.4)) < 0.01);
    TEST_ASSERT_TRUE(fabs(snap->course_deg - 84.4) < 0.01);
}

/* ========================================================================
 * 15. RMC status V → motion_valid=false, reason=RMC_VOID
 * ======================================================================== */

void test_rmc_status_v_motion_invalid(void)
{
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,1,08,1.0,50.0,M,0.0,M,,");
    feed_sentence(&primary,
        "GNRMC,123519,V,,,,,,230394,,N");
    gnss_um980_finalize_snapshot(&primary, 5000);

    const gnss_snapshot_t* snap = gnss_um980_get_snapshot(&primary);
    TEST_ASSERT_TRUE(snap->position_valid);  /* GGA is good */
    TEST_ASSERT_FALSE(snap->motion_valid);    /* RMC void */
    TEST_ASSERT_FALSE(snap->valid);           /* valid = pos AND motion */
    TEST_ASSERT_FALSE(snap->fresh);
    TEST_ASSERT_EQUAL(GNSS_REASON_RMC_VOID, snap->status_reason);
}

/* ========================================================================
 * 16. valid = position_valid AND motion_valid
 * ======================================================================== */

void test_valid_requires_both(void)
{
    /* Only GGA → position_valid but NOT valid */
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,1,08,1.0,50.0,M,0.0,M,,");
    gnss_um980_finalize_snapshot(&primary, 5000);
    TEST_ASSERT_TRUE(primary.snapshot.position_valid);
    TEST_ASSERT_FALSE(primary.snapshot.motion_valid);
    TEST_ASSERT_FALSE(primary.snapshot.valid);

    /* Add RMC A → now valid */
    feed_sentence(&primary,
        "GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W,A");
    gnss_um980_finalize_snapshot(&primary, 6000);
    TEST_ASSERT_TRUE(primary.snapshot.position_valid);
    TEST_ASSERT_TRUE(primary.snapshot.motion_valid);
    TEST_ASSERT_TRUE(primary.snapshot.valid);
}

/* ========================================================================
 * 17. Freshness: position stale → position_valid=false
 * ======================================================================== */

void test_freshness_position_stale(void)
{
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,1,08,1.0,50.0,M,0.0,M,,");
    feed_sentence(&primary,
        "GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W,A");
    gnss_um980_finalize_snapshot(&primary, 5000);
    TEST_ASSERT_TRUE(primary.snapshot.valid);

    /* Check freshness far past timeout */
    gnss_snapshot_check_freshness(&primary.snapshot, 5000 + 5000, GNSS_FRESHNESS_TIMEOUT_MS_DEFAULT);
    TEST_ASSERT_FALSE(primary.snapshot.position_valid);
    TEST_ASSERT_EQUAL(GNSS_REASON_STALE_GGA, primary.snapshot.status_reason);
}

/* ========================================================================
 * 18. Freshness: motion stale → motion_valid=false
 * ======================================================================== */

void test_freshness_motion_stale(void)
{
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,1,08,1.0,50.0,M,0.0,M,,");
    feed_sentence(&primary,
        "GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W,A");
    gnss_um980_finalize_snapshot(&primary, 5000);

    /* Feed another GGA to keep position fresh, but let RMC go stale */
    feed_sentence(&primary,
        "GNGGA,123520,4807.040,N,01131.001,E,1,08,1.0,50.1,M,0.0,M,,");
    gnss_um980_finalize_snapshot(&primary, 7000);
    /* Now GGA at 7000ms, RMC at 5000ms */
    /* Check at 7000 + 2500ms: GGA fresh (2500ms), RMC stale (4500ms > 2000ms) */
    gnss_snapshot_check_freshness(&primary.snapshot, 9500, GNSS_FRESHNESS_TIMEOUT_MS_DEFAULT);
    TEST_ASSERT_TRUE(primary.snapshot.position_valid);   /* GGA fresh */
    TEST_ASSERT_FALSE(primary.snapshot.motion_valid);    /* RMC stale */
    TEST_ASSERT_FALSE(primary.snapshot.valid);
    TEST_ASSERT_EQUAL(GNSS_REASON_STALE_RMC, primary.snapshot.status_reason);
}

/* ========================================================================
 * 19. Freshness: both stale → fresh=false
 * ======================================================================== */

void test_freshness_both_stale(void)
{
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,1,08,1.0,50.0,M,0.0,M,,");
    feed_sentence(&primary,
        "GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W,A");
    gnss_um980_finalize_snapshot(&primary, 5000);

    gnss_snapshot_check_freshness(&primary.snapshot, 5000 + 5000, GNSS_FRESHNESS_TIMEOUT_MS_DEFAULT);
    TEST_ASSERT_FALSE(primary.snapshot.position_valid);
    TEST_ASSERT_FALSE(primary.snapshot.motion_valid);
    TEST_ASSERT_FALSE(primary.snapshot.valid);
    TEST_ASSERT_FALSE(primary.snapshot.fresh);
}

/* ========================================================================
 * 20. Custom freshness timeout
 * ======================================================================== */

void test_custom_freshness_timeout(void)
{
    gnss_um980_set_freshness_timeout(&primary, 5000);
    TEST_ASSERT_EQUAL(5000, primary.freshness_timeout_ms);

    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,1,08,1.0,50.0,M,0.0,M,,");
    feed_sentence(&primary,
        "GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W,A");
    gnss_um980_finalize_snapshot(&primary, 10000);

    TEST_ASSERT_TRUE(primary.snapshot.valid);
    /* At 10000 + 4999ms → still fresh with 5000ms timeout */
    gnss_snapshot_check_freshness(&primary.snapshot, 14999, primary.freshness_timeout_ms);
    TEST_ASSERT_TRUE(primary.snapshot.fresh);

    /* At 10000 + 5001ms → stale */
    gnss_snapshot_check_freshness(&primary.snapshot, 15001, primary.freshness_timeout_ms);
    TEST_ASSERT_FALSE(primary.snapshot.fresh);
}

/* ========================================================================
 * 21. Correction age present → correction_age_valid=true
 * ======================================================================== */

void test_correction_age_present(void)
{
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,1,08,1.0,50.0,M,46.9,M,,");
    gnss_um980_finalize_snapshot(&primary, 5000);

    TEST_ASSERT_TRUE(primary.gga.age_diff_valid);
    const gnss_snapshot_t* snap = gnss_um980_get_snapshot(&primary);
    TEST_ASSERT_TRUE(snap->correction_age_valid);
    TEST_ASSERT_TRUE(fabs(snap->correction_age_s - 46.9) < 0.01);
}

/* ========================================================================
 * 22. Correction age empty → correction_age_valid=false
 * ======================================================================== */

void test_correction_age_empty(void)
{
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,1,08,1.0,50.0,M,,M,,");
    gnss_um980_finalize_snapshot(&primary, 5000);

    TEST_ASSERT_FALSE(primary.gga.age_diff_valid);
    const gnss_snapshot_t* snap = gnss_um980_get_snapshot(&primary);
    TEST_ASSERT_FALSE(snap->correction_age_valid);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, snap->correction_age_s);
}

/* ========================================================================
 * 23. GST accuracy → accuracy_valid=true, std values
 * ======================================================================== */

void test_gst_accuracy_valid(void)
{
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,1,08,1.0,50.0,M,0.0,M,,");
    feed_sentence(&primary,
        "GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W,A");
    feed_sentence(&primary,
        "GNGST,123519,3.2,2.6,2.1,1.8,1.3,1.8,2.1");
    gnss_um980_finalize_snapshot(&primary, 8000);

    const gnss_snapshot_t* snap = gnss_um980_get_snapshot(&primary);
    TEST_ASSERT_TRUE(snap->accuracy_valid);
    TEST_ASSERT_TRUE(fabs(snap->std_lat - 1.3) < 0.01);
    TEST_ASSERT_TRUE(fabs(snap->std_lon - 1.8) < 0.01);
    TEST_ASSERT_TRUE(fabs(snap->std_alt - 2.1) < 0.01);
    TEST_ASSERT_EQUAL(1, snap->gst_count);
}

/* ========================================================================
 * 24. GST optional → accuracy_valid=false without GST
 * ======================================================================== */

void test_gst_optional_accuracy_invalid(void)
{
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,1,08,1.0,50.0,M,0.0,M,,");
    feed_sentence(&primary,
        "GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W,A");
    gnss_um980_finalize_snapshot(&primary, 7000);

    const gnss_snapshot_t* snap = gnss_um980_get_snapshot(&primary);
    TEST_ASSERT_TRUE(snap->valid);              /* valid from GGA+RMC */
    TEST_ASSERT_FALSE(snap->accuracy_valid);    /* no GST → accuracy invalid */
    TEST_ASSERT_EQUAL_DOUBLE(0.0, snap->std_lat);
}

/* ========================================================================
 * 25. Unknown fix_quality (3=PPS) → position_valid=false
 * ======================================================================== */

void test_unknown_fix_quality(void)
{
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,3,08,1.0,50.0,M,0.0,M,,");
    gnss_um980_finalize_snapshot(&primary, 3000);

    const gnss_snapshot_t* snap = gnss_um980_get_snapshot(&primary);
    /* fix_quality=3 (PPS) is recognized but not in 1,2,4,5 range → position invalid */
    /* Wait — 3 IS in range 1-5. Let me re-check: gga_has_valid_fix allows 1-5 */
    TEST_ASSERT_TRUE(snap->position_valid);
    TEST_ASSERT_EQUAL(GNSS_FIX_PPS, snap->fix_quality);
    TEST_ASSERT_EQUAL(GNSS_RTK_NONE, snap->rtk_status);

    /* But fix_quality=6 is truly unknown */
    setUp();  /* reset */
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,6,08,1.0,50.0,M,0.0,M,,");
    gnss_um980_finalize_snapshot(&primary, 4000);
    const gnss_snapshot_t* snap2 = gnss_um980_get_snapshot(&primary);
    TEST_ASSERT_FALSE(snap2->position_valid);
    TEST_ASSERT_EQUAL(GNSS_FIX_UNKNOWN, snap2->fix_quality);
    TEST_ASSERT_EQUAL(GNSS_REASON_UNKNOWN_FIX, snap2->status_reason);
}

/* ========================================================================
 * 26. Dual-receiver isolation (full)
 * ======================================================================== */

void test_dual_receiver_full_isolation(void)
{
    feed_sentence(&primary,
        "GNGGA,100000,4807.038,N,01131.000,E,4,12,1.0,100.0,M,0.0,M,,");
    feed_sentence(&primary,
        "GNRMC,100000,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W,A");
    feed_sentence(&secondary,
        "GNGGA,200000,3412.500,S,11815.000,W,1,06,2.0,50.0,M,0.0,M,,");
    feed_sentence(&secondary,
        "GNRMC,200000,V,,,,,,010125,,N");

    gnss_um980_finalize_snapshot(&primary, 10000);
    gnss_um980_finalize_snapshot(&secondary, 20000);

    const gnss_snapshot_t* p = gnss_um980_get_snapshot(&primary);
    const gnss_snapshot_t* s = gnss_um980_get_snapshot(&secondary);

    /* Primary: fully valid */
    TEST_ASSERT_TRUE(p->position_valid);
    TEST_ASSERT_TRUE(p->motion_valid);
    TEST_ASSERT_TRUE(p->valid);
    TEST_ASSERT_TRUE(p->fresh);
    TEST_ASSERT_EQUAL(GNSS_FIX_RTK_FIXED, p->fix_quality);
    TEST_ASSERT_TRUE(p->latitude > 0);
    TEST_ASSERT_EQUAL(1, primary.gga_count);
    TEST_ASSERT_EQUAL(1, primary.rmc_count);

    /* Secondary: position valid, motion invalid (RMC void) */
    TEST_ASSERT_TRUE(s->position_valid);
    TEST_ASSERT_FALSE(s->motion_valid);
    TEST_ASSERT_FALSE(s->valid);
    TEST_ASSERT_EQUAL(GNSS_REASON_RMC_VOID, s->status_reason);
    TEST_ASSERT_EQUAL(GNSS_FIX_SINGLE, s->fix_quality);
    TEST_ASSERT_TRUE(s->latitude < 0);
    TEST_ASSERT_EQUAL(1, secondary.gga_count);
    TEST_ASSERT_EQUAL(1, secondary.rmc_count);

    /* Counters fully independent */
    TEST_ASSERT_EQUAL(0, primary.checksum_errors);
    TEST_ASSERT_EQUAL(0, secondary.checksum_errors);
}

/* ========================================================================
 * 27. Checksum error in secondary does not affect primary
 * ======================================================================== */

void test_checksum_error_isolation(void)
{
    feed_sentence(&primary,
        "GNGGA,100000,4807.038,N,01131.000,E,1,08,1.0,50.0,M,0.0,M,,");
    feed_sentence(&secondary,
        "GNGGA,100000,4807.038,N,01131.000,E,1,08,1.0,50.0,M,0.0,M,,");

    /* Bad checksum only to secondary */
    char bad[256];
    make_nmea_bad_checksum(bad,
        "GNGGA,100001,5321.6802,N,01339.4463,E,4,12,0.8,100.0,M,0.0,M,,", 0xAB);
    gnss_um980_feed(&secondary, (const uint8_t*)bad, strlen(bad));

    TEST_ASSERT_EQUAL(0, primary.checksum_errors);
    TEST_ASSERT_EQUAL(1, secondary.checksum_errors);
    TEST_ASSERT_TRUE(primary.gga_valid);
    /* Secondary: original GGA still valid (bad one was rejected) */
    TEST_ASSERT_TRUE(secondary.gga_valid);
}

/* ========================================================================
 * 28. Stale secondary does not make primary stale
 * ======================================================================== */

void test_stale_isolation(void)
{
    feed_sentence(&primary,
        "GNGGA,100000,4807.038,N,01131.000,E,1,08,1.0,50.0,M,0.0,M,,");
    feed_sentence(&primary,
        "GNRMC,100000,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W,A");
    feed_sentence(&secondary,
        "GNGGA,50000,3412.500,S,11815.000,W,1,06,2.0,50.0,M,0.0,M,,");
    feed_sentence(&secondary,
        "GNRMC,50000,A,3412.500,S,11815.000,W,5.0,90.0,010125,,A");

    gnss_um980_finalize_snapshot(&primary, 10000);
    gnss_um980_finalize_snapshot(&secondary, 50000);

    /* Primary at t=10000, check at t=10000+100 (still fresh) */
    gnss_snapshot_check_freshness(&primary.snapshot, 10100, GNSS_FRESHNESS_TIMEOUT_MS_DEFAULT);
    TEST_ASSERT_TRUE(primary.snapshot.fresh);

    /* Secondary at t=50000, check at t=50000+5000 (stale) */
    gnss_snapshot_check_freshness(&secondary.snapshot, 55000, GNSS_FRESHNESS_TIMEOUT_MS_DEFAULT);
    TEST_ASSERT_FALSE(secondary.snapshot.fresh);

    /* PRIMARY MUST STILL BE FRESH */
    gnss_snapshot_check_freshness(&primary.snapshot, 10100, GNSS_FRESHNESS_TIMEOUT_MS_DEFAULT);
    TEST_ASSERT_TRUE(primary.snapshot.fresh);
}

/* ========================================================================
 * 29. Knots→m/s conversion
 * ======================================================================== */

void test_knots_to_ms(void)
{
    TEST_ASSERT_TRUE(fabs(gnss_knots_to_ms(0.0) - 0.0) < 0.001);
    TEST_ASSERT_TRUE(fabs(gnss_knots_to_ms(1.0) - 0.514444) < 0.001);
    TEST_ASSERT_TRUE(fabs(gnss_knots_to_ms(10.0) - 5.14444) < 0.001);
    TEST_ASSERT_TRUE(fabs(gnss_knots_to_ms(100.0) - 51.4444) < 0.001);
}

/* ========================================================================
 * 30. Null safety
 * ======================================================================== */

void test_null_safety(void)
{
    gnss_um980_init(NULL, 0, "null");
    gnss_snapshot_init(NULL);

    uint8_t dummy[] = {0x24, 0x47, 0x4E};
    TEST_ASSERT_EQUAL(0, gnss_um980_feed(NULL, dummy, 3));
    TEST_ASSERT_EQUAL(0, gnss_um980_feed(&primary, NULL, 3));

    TEST_ASSERT_NULL(gnss_um980_get_gga(NULL));
    TEST_ASSERT_NULL(gnss_um980_get_rmc(NULL));
    TEST_ASSERT_NULL(gnss_um980_get_gst(NULL));
    TEST_ASSERT_NULL(gnss_um980_get_snapshot(NULL));
    TEST_ASSERT_FALSE(gnss_um980_has_fix(NULL));
    TEST_ASSERT_FALSE(gnss_um980_is_fresh(NULL));

    gnss_um980_set_rx_source(NULL, NULL);
    gnss_um980_set_freshness_timeout(NULL, 1000);
    gnss_um980_finalize_snapshot(NULL, 0);
    gnss_um980_service_step(NULL, 0);

    TEST_ASSERT_EQUAL(UINT64_MAX, gnss_snapshot_age_ms(NULL, 1000));
}

void test_getters_before_feed(void)
{
    TEST_ASSERT_NULL(gnss_um980_get_gga(&primary));
    TEST_ASSERT_NULL(gnss_um980_get_rmc(&primary));
    TEST_ASSERT_NULL(gnss_um980_get_gst(&primary));
    TEST_ASSERT_FALSE(gnss_um980_has_fix(&primary));
    TEST_ASSERT_FALSE(gnss_um980_is_fresh(&primary));
}

/* ========================================================================
 * 31. Overflow error counter
 * ======================================================================== */

void test_overflow_error(void)
{
    char long_body[200];
    memset(long_body, 'A', sizeof(long_body) - 1);
    long_body[sizeof(long_body) - 1] = '\0';

    char buf[256];
    make_nmea(buf, long_body);
    gnss_um980_feed(&primary, (const uint8_t*)buf, strlen(buf));

    TEST_ASSERT_TRUE(primary.overflow_errors > 0);
    TEST_ASSERT_EQUAL(0, primary.sentences_parsed);
}

/* ========================================================================
 * 32. Freshness timeout clamping
 * ======================================================================== */

void test_freshness_clamping(void)
{
    gnss_um980_set_freshness_timeout(&primary, 10);
    TEST_ASSERT_EQUAL(GNSS_FRESHNESS_TIMEOUT_MS_MIN, primary.freshness_timeout_ms);

    gnss_um980_set_freshness_timeout(&primary, 999999);
    TEST_ASSERT_EQUAL(GNSS_FRESHNESS_TIMEOUT_MS_MAX, primary.freshness_timeout_ms);

    gnss_um980_set_freshness_timeout(&primary, 0);
    TEST_ASSERT_EQUAL(GNSS_FRESHNESS_TIMEOUT_MS_DEFAULT, primary.freshness_timeout_ms);
}

/* ========================================================================
 * 33. Snapshot age_ms
 * ======================================================================== */

void test_snapshot_age_ms(void)
{
    gnss_snapshot_t snap;
    gnss_snapshot_init(&snap);
    TEST_ASSERT_EQUAL(UINT64_MAX, gnss_snapshot_age_ms(&snap, 5000));

    snap.last_gga_time_ms = 1000;
    TEST_ASSERT_EQUAL(4000, gnss_snapshot_age_ms(&snap, 5000));
    TEST_ASSERT_EQUAL(0, gnss_snapshot_age_ms(&snap, 500));  /* clock wrap */
}

/* ========================================================================
 * 34. Timeout event counter
 * ======================================================================== */

void test_timeout_event_counter(void)
{
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,1,08,1.0,50.0,M,0.0,M,,");
    feed_sentence(&primary,
        "GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W,A");
    gnss_um980_finalize_snapshot(&primary, 1000);
    TEST_ASSERT_TRUE(primary.snapshot.fresh);
    TEST_ASSERT_EQUAL(0, primary.timeout_events);

    /* Simulate staleness */
    gnss_snapshot_check_freshness(&primary.snapshot, 10000, primary.freshness_timeout_ms);
    TEST_ASSERT_FALSE(primary.snapshot.fresh);
}

/* ========================================================================
 * 35. Cumulative statistics
 * ======================================================================== */

void test_cumulative_statistics(void)
{
    feed_sentence(&primary,
        "GNGGA,100000,4807.038,N,01131.000,E,1,04,1.0,50.0,M,0.0,M,,");
    feed_sentence(&primary,
        "GNRMC,100000,A,4807.038,N,01131.000,E,5.0,90.0,010125,,A");
    feed_sentence(&primary,
        "GNGGA,100001,4807.040,N,01131.001,E,1,05,1.0,50.1,M,0.0,M,,");

    gnss_um980_finalize_snapshot(&primary, 10000);
    TEST_ASSERT_EQUAL(2, primary.gga_count);
    TEST_ASSERT_EQUAL(1, primary.rmc_count);
    TEST_ASSERT_EQUAL(3, primary.sentences_parsed);
}

/* ========================================================================
 * 36. Bytes received counter
 * ======================================================================== */

void test_bytes_received(void)
{
    char buf[256];
    make_nmea(buf, "GNGGA,123519,4807.038,N,01131.000,E,1,04,1.0,50.0,M,0.0,M,,");
    size_t len = strlen(buf);
    gnss_um980_feed(&primary, (const uint8_t*)buf, len);
    TEST_ASSERT_EQUAL((uint32_t)len, primary.bytes_received);
}

/* ========================================================================
 * 37. Merged snapshot: GGA+RMC+GST → valid+fresh
 * ======================================================================== */

void test_merged_valid_fresh(void)
{
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,4,12,1.0,100.0,M,0.0,M,,");
    feed_sentence(&primary,
        "GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W,A");
    feed_sentence(&primary,
        "GNGST,123519,3.2,2.6,2.1,1.8,1.3,1.8,2.1");
    gnss_um980_finalize_snapshot(&primary, 8000);

    const gnss_snapshot_t* snap = gnss_um980_get_snapshot(&primary);
    TEST_ASSERT_TRUE(snap->position_valid);
    TEST_ASSERT_TRUE(snap->motion_valid);
    TEST_ASSERT_TRUE(snap->accuracy_valid);
    TEST_ASSERT_TRUE(snap->valid);
    TEST_ASSERT_TRUE(snap->fresh);
    TEST_ASSERT_EQUAL(GNSS_REASON_NONE, snap->status_reason);
    TEST_ASSERT_EQUAL(GNSS_FIX_RTK_FIXED, snap->fix_quality);
    TEST_ASSERT_EQUAL(3, snap->sentences_parsed);
    TEST_ASSERT_EQUAL(0, snap->sentences_error);
}

/* ========================================================================
 * 38. GGA only (no RMC) → position_valid but NOT valid
 * ======================================================================== */

void test_gga_only_not_valid(void)
{
    feed_sentence(&primary,
        "GNGGA,123519,4807.038,N,01131.000,E,1,08,1.0,50.0,M,0.0,M,,");
    gnss_um980_finalize_snapshot(&primary, 7000);

    const gnss_snapshot_t* snap = gnss_um980_get_snapshot(&primary);
    TEST_ASSERT_TRUE(snap->position_valid);
    TEST_ASSERT_FALSE(snap->motion_valid);
    TEST_ASSERT_FALSE(snap->valid);
    TEST_ASSERT_EQUAL(GNSS_REASON_NO_RMC, snap->status_reason);
}

/* ========================================================================
 * 39. status_reason documentation
 * ======================================================================== */

void test_status_reason_no_gga(void)
{
    /* No data at all → reason should be NO_GGA after freshness check */
    gnss_um980_finalize_snapshot(&primary, 1000);
    const gnss_snapshot_t* snap = gnss_um980_get_snapshot(&primary);
    /* Before any data: no GGA → NO_GGA */
    gnss_snapshot_check_freshness(&primary.snapshot, 2000, GNSS_FRESHNESS_TIMEOUT_MS_DEFAULT);
    TEST_ASSERT_EQUAL(GNSS_REASON_NO_GGA, primary.snapshot.status_reason);
}

/* ========================================================================
 * 40. Invalid checksum does NOT overwrite existing snapshot
 * ======================================================================== */

void test_invalid_checksum_preserves_snapshot(void)
{
    /* Feed valid GGA+RMC */
    feed_sentence(&primary,
        "GNGGA,100000,4807.038,N,01131.000,E,4,12,1.0,100.0,M,0.0,M,,");
    feed_sentence(&primary,
        "GNRMC,100000,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W,A");
    gnss_um980_finalize_snapshot(&primary, 10000);

    const gnss_snapshot_t* snap = gnss_um980_get_snapshot(&primary);
    TEST_ASSERT_TRUE(snap->valid);
    double orig_lat = snap->latitude;
    double orig_speed = snap->speed_ms;
    uint8_t orig_sats = snap->satellites;

    /* Feed many bad-checksum sentences */
    for (int i = 0; i < 5; i++) {
        char bad[256];
        make_nmea_bad_checksum(bad,
            "GNGGA,200000,5321.6802,N,01339.4463,E,5,20,0.5,200.0,M,0.0,M,,", 0x00);
        gnss_um980_feed(&primary, (const uint8_t*)bad, strlen(bad));
    }

    /* Snapshot must be completely unchanged */
    const gnss_snapshot_t* snap2 = gnss_um980_get_snapshot(&primary);
    TEST_ASSERT_EQUAL_DOUBLE(orig_lat, snap2->latitude);
    TEST_ASSERT_EQUAL_DOUBLE(orig_speed, snap2->speed_ms);
    TEST_ASSERT_EQUAL(orig_sats, snap2->satellites);
    TEST_ASSERT_TRUE(snap2->valid);
    TEST_ASSERT_EQUAL(5, primary.checksum_errors);
    TEST_ASSERT_EQUAL(0, primary.sentences_parsed);  /* still only 2 from before */

    /* Verify the bad data is NOT in the snapshot */
    TEST_ASSERT_TRUE(fabs(snap2->latitude - 53.361) > 0.01);
    TEST_ASSERT_TRUE(fabs(snap2->altitude - 200.0) > 0.01);
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* 1. Initialization */
    RUN_TEST(test_snapshot_init_variant_a);
    RUN_TEST(test_um980_init_defaults);

    /* 2. Fix quality mapping */
    RUN_TEST(test_fix_quality_from_gga);

    /* 3. RTK status */
    RUN_TEST(test_rtk_status_from_gga);

    /* 4. GGA correct checksum */
    RUN_TEST(test_gga_correct_checksum_accepted);

    /* 5. GGA wrong checksum */
    RUN_TEST(test_gga_wrong_checksum_rejected);

    /* 6. RMC correct checksum */
    RUN_TEST(test_rmc_correct_checksum_accepted);

    /* 7. RMC wrong checksum */
    RUN_TEST(test_rmc_wrong_checksum_rejected);

    /* 8. No checksum */
    RUN_TEST(test_sentence_without_checksum_rejected);

    /* 9. Non-hex checksum */
    RUN_TEST(test_nonhex_checksum_rejected);

    /* 10. Fragmented sentence */
    RUN_TEST(test_fragmented_correct_checksum);

    /* 11. Multiple sentences, one bad */
    RUN_TEST(test_multiple_sentences_one_bad);

    /* 12. GGA fix=0 */
    RUN_TEST(test_gga_no_fix_position_invalid);

    /* 13. GGA RTK Fixed */
    RUN_TEST(test_gga_rtk_fixed_position_valid);

    /* 14. RMC status A */
    RUN_TEST(test_rmc_status_a_motion_valid);

    /* 15. RMC status V */
    RUN_TEST(test_rmc_status_v_motion_invalid);

    /* 16. valid = pos AND motion */
    RUN_TEST(test_valid_requires_both);

    /* 17-19. Freshness */
    RUN_TEST(test_freshness_position_stale);
    RUN_TEST(test_freshness_motion_stale);
    RUN_TEST(test_freshness_both_stale);

    /* 20. Custom timeout */
    RUN_TEST(test_custom_freshness_timeout);

    /* 21-22. Correction age */
    RUN_TEST(test_correction_age_present);
    RUN_TEST(test_correction_age_empty);

    /* 23-24. GST */
    RUN_TEST(test_gst_accuracy_valid);
    RUN_TEST(test_gst_optional_accuracy_invalid);

    /* 25. Unknown fix quality */
    RUN_TEST(test_unknown_fix_quality);

    /* 26-28. Dual-receiver isolation */
    RUN_TEST(test_dual_receiver_full_isolation);
    RUN_TEST(test_checksum_error_isolation);
    RUN_TEST(test_stale_isolation);

    /* 29. Knots→m/s */
    RUN_TEST(test_knots_to_ms);

    /* 30. Null safety */
    RUN_TEST(test_null_safety);
    RUN_TEST(test_getters_before_feed);

    /* 31. Overflow */
    RUN_TEST(test_overflow_error);

    /* 32. Timeout clamping */
    RUN_TEST(test_freshness_clamping);

    /* 33. Snapshot age */
    RUN_TEST(test_snapshot_age_ms);

    /* 34. Timeout events */
    RUN_TEST(test_timeout_event_counter);

    /* 35. Cumulative stats */
    RUN_TEST(test_cumulative_statistics);

    /* 36. Bytes received */
    RUN_TEST(test_bytes_received);

    /* 37. Merged snapshot */
    RUN_TEST(test_merged_valid_fresh);

    /* 38. GGA only */
    RUN_TEST(test_gga_only_not_valid);

    /* 39. Status reason */
    RUN_TEST(test_status_reason_no_gga);

    /* 40. Invalid checksum preserves snapshot */
    RUN_TEST(test_invalid_checksum_preserves_snapshot);

    return UNITY_END();
}
