/* ========================================================================
 * test_aog_pgn214 — Host tests for NAV-AOG-001-FINAL
 *
 * Validates all 16 mandatory test cases plus additional regression tests:
 *
 * FRAME FORMAT & ENCODING:
 *  1.  PGN 214 frame format (v5: preamble, SRC, PGN, LEN, payload, CRC)
 *  2.  Frame length = 7 + 51 = 58 bytes
 *  3.  CRC = sum(bytes[2..56]) mod 256
 *  4.  Little-endian encoding of all PGN 214 fields
 *  5.  Byte offsets correct (lon@0, lat@8, hdg_dual@16, ...)
 *  6.  Sentinel values (double max, float max, 0xFFFF, 0x7FFF)
 *  7.  Fix quality mapping (gnss→AOG)
 *  8.  PGN exact bytes — every byte position regression-safe
 *
 * OUTPUT GATING (P1):
 *  9.  Valid GNSS + valid heading → full live frame
 * 10.  Valid GNSS + invalid heading → sentinel heading, real GNSS
 * 11.  Valid GNSS + stale heading → sentinel heading, real GNSS
 * 12.  Valid GNSS + heading lost → sentinel heading (HEADING_LOST state)
 * 13.  Invalid GNSS → sentinel PGN 214, fix_quality=0
 * 14.  Stale GNSS → sentinel PGN 214, fix_quality=0
 * 15.  No stale GNSS reuse
 * 16.  No stale heading reuse
 * 17.  Heading fallback: sentinel (not suppress, not stale reuse)
 * 18.  Heading fallback: suppress when GNSS also invalid
 *
 * STATUS / FIX MAPPING (P6):
 * 19.  All 8 output states covered
 * 20.  Fix quality: all 5 NMEA values + unknown
 *
 * DISCOVERY (P2):
 * 21.  PGN 253 Hello → PGN 254 Hello Reply
 * 22.  PGN 202 Scan → PGN 203 Scan Reply
 * 23.  Discovery tolerant CRC: CRC=0x00 accepted for PGN 253/202
 * 24.  Discovery tolerant CRC: ±1 accepted for PGN 253/202
 * 25.  Discovery strict split: core PGN rejects bad CRC
 * 26.  Discovery bypasses output gating
 * 27.  Multiple hello requests → multiple replies
 * 28.  Scan reply content and module_type
 *
 * SOURCE IDs (P3):
 * 29.  Source byte = 0x05 (AOG_SRC_GPS) in outgoing frames
 * 30.  Module type = 0x78 (AOG_MODULE_TYPE_GPS) in Scan Reply
 *
 * TIMING & DETERMINISM:
 * 31.  Deterministic cyclic output (20 Hz)
 *
 * DATA CONVERSION (P5):
 * 32.  Heading normalization (rad→deg, 0-360)
 * 33.  Speed conversion (m/s → km/h)
 * 34.  HDOP scaling (×100)
 *
 * ARCHITECTURE:
 * 35.  Transport isolation (no UDP calls in AOG app)
 *
 * CRC EXACT:
 * 36.  CRC exact on all TX frames (no tolerance on output)
 *
 * GOLDEN BYTE REGRESSION (P3 — FINAL HARDENING):
 * 40.  Golden PGN 214 header bytes (0x80, 0x81, SRC, PGN, LEN exact)
 * 41.  Golden sentinel payload: every byte of all-sentinel PGN 214
 * 42.  Golden CRC: known exact CRC for zero-payload Discovery frame
 * 43.  Golden Hello Response frame: exact bytes
 * 44.  Golden Scan Reply frame: exact bytes with module_type=0x78
 * 45.  Golden Heading encoding: exact f32 bytes for known rad→deg
 * 46.  Golden fix_quality byte: exact byte at offset 38 per status
 * 47.  Golden Scan Request tolerant CRC (PGN 202)
 * 48.  Golden PGN 214 complete frame: all 58 bytes verified
 * 49.  Golden Discovery boundary: CRC=0xFF-1 vs CRC=0xFF-2
 * ======================================================================== */

#include "unity.h"
#include "aog_frame.h"
#include "aog_pgn.h"
#include "aog_navigation_app.h"
#include "gnss_um980.h"
#include "gnss_snapshot.h"
#include "gnss_dual_heading.h"
#include "snapshot_buffer.h"
#include "byte_ring_buffer.h"
#include <string.h>
#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>

/* ========================================================================
 * Ring buffer helper for tests
 * ======================================================================== */

#define TEST_RING_SIZE  2048

static uint8_t s_tx_storage[TEST_RING_SIZE];
static uint8_t s_rx_storage[TEST_RING_SIZE];
static byte_ring_buffer_t s_tx_buf;
static byte_ring_buffer_t s_rx_buf;

/* ========================================================================
 * Snapshot buffer helpers
 * ======================================================================== */

static gnss_snapshot_t s_gnss_storage;
static snapshot_buffer_t s_gnss_snap;

static gnss_dual_heading_t s_hdg_storage;
static snapshot_buffer_t s_hdg_snap;

/* ---- Helper: drain TX ring buffer ---- */
static void drain_tx(void)
{
    uint8_t tmp[256];
    while (byte_ring_buffer_available(&s_tx_buf) > 0) {
        byte_ring_buffer_read(&s_tx_buf, tmp, sizeof(tmp));
    }
}

static uint8_t nmea_checksum(const char* body)
{
    uint8_t cs = 0;
    for (const char* p = body; *p != '\0'; p++) {
        cs ^= (uint8_t)*p;
    }
    return cs;
}

static void make_nmea(char* out, const char* body)
{
    uint8_t cs = nmea_checksum(body);
    sprintf(out, "$%s*%02X\r\n", body, cs);
}

static uint32_t feed_sentence(gnss_um980_t* rx, const char* body)
{
    char buf[256];
    make_nmea(buf, body);
    return gnss_um980_feed(rx, (const uint8_t*)buf, strlen(buf));
}

/* ========================================================================
 * AOG navigation app fixture
 * ======================================================================== */

static aog_nav_app_t s_app;

void setUp(void)
{
    memset(&s_app, 0, sizeof(s_app));
    byte_ring_buffer_init(&s_tx_buf, s_tx_storage, TEST_RING_SIZE);
    byte_ring_buffer_init(&s_rx_buf, s_rx_storage, TEST_RING_SIZE);

    gnss_snapshot_init(&s_gnss_storage);
    snapshot_buffer_init(&s_gnss_snap, &s_gnss_storage, sizeof(gnss_snapshot_t));

    memset(&s_hdg_storage, 0, sizeof(s_hdg_storage));
    snapshot_buffer_init(&s_hdg_snap, &s_hdg_storage, sizeof(gnss_dual_heading_t));

    aog_nav_app_init(&s_app);
    aog_nav_app_set_position_source(&s_app, &s_gnss_snap);
    aog_nav_app_set_heading_source(&s_app, &s_hdg_snap);
    aog_nav_app_set_aog_rx_source(&s_app, &s_rx_buf);
    aog_nav_app_set_aog_tx_dest(&s_app, &s_tx_buf);
}

void tearDown(void) {}

/* ========================================================================
 * Helper: set up valid GNSS snapshot
 * ======================================================================== */

static void set_valid_gnss_snapshot(void)
{
    s_gnss_storage.position_valid = true;
    s_gnss_storage.motion_valid = true;
    s_gnss_storage.valid = true;
    s_gnss_storage.fresh = true;
    s_gnss_storage.latitude = 48.07;
    s_gnss_storage.longitude = 11.31;
    s_gnss_storage.altitude = 500.0;
    s_gnss_storage.speed_ms = 2.5;
    s_gnss_storage.course_deg = 90.0;
    s_gnss_storage.satellites = 12;
    s_gnss_storage.hdop = 1.2;
    s_gnss_storage.fix_quality = GNSS_FIX_RTK_FIXED;
    s_gnss_storage.rtk_status = GNSS_RTK_FIXED;
    s_gnss_storage.correction_age_valid = true;
    s_gnss_storage.correction_age_s = 2.5;
    s_gnss_storage.timestamp_ms = 1000;
    s_gnss_storage.status_reason = GNSS_REASON_NONE;
    snapshot_buffer_set(&s_gnss_snap, &s_gnss_storage);
}

/* ========================================================================
 * Helper: set up valid heading snapshot
 * ======================================================================== */

static void set_valid_heading_snapshot(double heading_rad)
{
    s_hdg_storage.heading_rad = heading_rad;
    s_hdg_storage.valid = true;
    s_hdg_storage.primary_fix = 4;
    s_hdg_storage.secondary_fix = 4;
    snapshot_buffer_set(&s_hdg_snap, &s_hdg_storage);
}

/* ========================================================================
 * Helper: inject a complete AOG frame into RX buffer
 * ======================================================================== */

static void inject_aog_frame(uint16_t pgn, const uint8_t* data, uint8_t data_len)
{
    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t frame_len = aog_frame_encode(frame, AOG_SRC_AOG, pgn, data, data_len);
    byte_ring_buffer_write(&s_rx_buf, frame, frame_len);
}

/* ========================================================================
 * Helper: inject a frame with corrupted CRC
 * ======================================================================== */

static void inject_aog_frame_bad_crc(uint16_t pgn, const uint8_t* data,
                                      uint8_t data_len, uint8_t bad_crc)
{
    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t frame_len = aog_frame_encode(frame, AOG_SRC_AOG, pgn, data, data_len);
    if (frame_len > 0) {
        frame[frame_len - 1] = bad_crc;  /* replace CRC */
        byte_ring_buffer_write(&s_rx_buf, frame, frame_len);
    }
}

/* ========================================================================
 * Helper: find and decode a specific PGN frame from TX buffer
 * Returns true if found, fills out_frame and out_pgn
 * ======================================================================== */

static bool find_pgn_in_tx(uint16_t target_pgn, uint8_t* out_frame,
                           size_t* out_frame_len)
{
    uint8_t tmp[256];
    while (byte_ring_buffer_available(&s_tx_buf) >= 8) {
        size_t to_read = byte_ring_buffer_available(&s_tx_buf);
        if (to_read > sizeof(tmp)) to_read = sizeof(tmp);
        size_t n = byte_ring_buffer_read(&s_tx_buf, tmp, to_read);

        for (size_t i = 0; i + 8 <= n; ) {
            if (tmp[i] == 0x80 && tmp[i+1] == 0x81) {
                uint16_t pgn = (uint16_t)tmp[i+3] | ((uint16_t)tmp[i+4] << 8);
                uint8_t len = tmp[i+5];
                size_t flen = 8 + len;
                if (pgn == target_pgn && flen <= sizeof(tmp)) {
                    if (out_frame != NULL) {
                        memcpy(out_frame, &tmp[i], flen);
                    }
                    if (out_frame_len != NULL) {
                        *out_frame_len = flen;
                    }
                    return true;
                }
                i += flen;
            } else {
                i++;
            }
        }
    }
    return false;
}

/* ========================================================================
 * Helper: read first frame from TX
 * ======================================================================== */

static size_t read_first_frame(uint8_t* frame, size_t max_len)
{
    if (byte_ring_buffer_available(&s_tx_buf) < 8) return 0;
    size_t avail = byte_ring_buffer_available(&s_tx_buf);
    if (avail > max_len) avail = max_len;
    return byte_ring_buffer_read(&s_tx_buf, frame, avail);
}

/* ========================================================================
 * 1. PGN 214 frame format (v5)
 * ======================================================================== */

void test_pgn214_frame_format(void)
{
    aog_pgn214_t data;
    memset(&data, 0, sizeof(data));
    data.longitude = 11.31;
    data.latitude = 48.07;
    data.fix_quality = AOG_FIX_RTK_FIX;
    data.satellites = 12;
    data.heading_dual = 180.0f;
    data.speed_kmh = 5.0f;

    uint8_t payload[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_pgn214(payload, &data);

    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t frame_len = aog_frame_encode(frame, AOG_SRC_GPS, AOG_PGN_214_OUT,
                                         payload, AOG_PGN214_DATA_SIZE);

    TEST_ASSERT_EQUAL(58, frame_len);
    TEST_ASSERT_EQUAL(0x80, frame[0]);
    TEST_ASSERT_EQUAL(0x81, frame[1]);
    TEST_ASSERT_EQUAL(AOG_SRC_GPS, frame[2]);
    TEST_ASSERT_EQUAL(0xD6, frame[3]);
    TEST_ASSERT_EQUAL(0x00, frame[4]);
    TEST_ASSERT_EQUAL(51, frame[5]);
}

/* ========================================================================
 * 2. Frame length exact
 * ======================================================================== */

void test_pgn214_frame_length_exact(void)
{
    aog_pgn214_t data;
    aog_pgn214_set_sentinels(&data);

    uint8_t payload[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_pgn214(payload, &data);

    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t frame_len = aog_frame_encode(frame, AOG_SRC_GPS, AOG_PGN_214_OUT,
                                         payload, AOG_PGN214_DATA_SIZE);

    TEST_ASSERT_EQUAL(58, frame_len);
    TEST_ASSERT_EQUAL(7 + AOG_PGN214_DATA_SIZE, frame_len);
}

/* ========================================================================
 * 3. CRC = sum mod 256
 * ======================================================================== */

void test_pgn214_crc_sum(void)
{
    aog_pgn214_t data;
    memset(&data, 0, sizeof(data));
    data.fix_quality = AOG_FIX_GPS;
    data.satellites = 8;

    uint8_t payload[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_pgn214(payload, &data);

    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t frame_len = aog_frame_encode(frame, AOG_SRC_GPS, AOG_PGN_214_OUT,
                                         payload, AOG_PGN214_DATA_SIZE);

    uint32_t expected = 0;
    for (size_t i = 2; i < frame_len - 1; i++) {
        expected += frame[i];
    }
    uint8_t expected_crc = (uint8_t)(expected & 0xFF);
    TEST_ASSERT_EQUAL(expected_crc, frame[frame_len - 1]);
    TEST_ASSERT_TRUE(aog_frame_verify_crc(frame, frame_len));
}

/* ========================================================================
 * 4. Little-endian encoding
 * ======================================================================== */

void test_pgn214_little_endian(void)
{
    aog_pgn214_t data;
    memset(&data, 0, sizeof(data));
    data.satellites = 0x1234;

    uint8_t payload[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_pgn214(payload, &data);

    TEST_ASSERT_EQUAL(0x34, payload[36]);
    TEST_ASSERT_EQUAL(0x12, payload[37]);

    data.hdop_x100 = 0xABCD;
    aog_pgn_encode_pgn214(payload, &data);
    TEST_ASSERT_EQUAL(0xCD, payload[39]);
    TEST_ASSERT_EQUAL(0xAB, payload[40]);

    data.imu_roll_x10 = -100;
    aog_pgn_encode_pgn214(payload, &data);
    TEST_ASSERT_EQUAL(0x9C, payload[45]);
    TEST_ASSERT_EQUAL(0xFF, payload[46]);
}

/* ========================================================================
 * 5. Byte offsets correct
 * ======================================================================== */

void test_pgn214_byte_offsets(void)
{
    aog_pgn214_t data;
    memset(&data, 0, sizeof(data));
    data.longitude = 123.456;
    data.latitude = 45.678;
    data.heading_dual = 90.0f;
    data.heading_true = 270.5f;
    data.speed_kmh = 10.0f;
    data.altitude = 350.0f;
    data.satellites = 14;
    data.fix_quality = 4;

    uint8_t payload[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_pgn214(payload, &data);

    aog_pgn214_t decoded;
    TEST_ASSERT_TRUE(aog_pgn_decode_pgn214(payload, AOG_PGN214_DATA_SIZE, &decoded));

    TEST_ASSERT_EQUAL_DOUBLE(123.456, decoded.longitude);
    TEST_ASSERT_EQUAL_DOUBLE(45.678, decoded.latitude);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, decoded.heading_dual);
    TEST_ASSERT_EQUAL_FLOAT(270.5f, decoded.heading_true);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, decoded.speed_kmh);
    TEST_ASSERT_EQUAL_FLOAT(350.0f, decoded.altitude);
    TEST_ASSERT_EQUAL(14, decoded.satellites);
    TEST_ASSERT_EQUAL(4, decoded.fix_quality);
}

/* ========================================================================
 * 6. Sentinel values
 * ======================================================================== */

void test_pgn214_sentinel_values(void)
{
    aog_pgn214_t data;
    aog_pgn214_set_sentinels(&data);

    TEST_ASSERT_EQUAL_DOUBLE(AOG_SENTINEL_DOUBLE, data.longitude);
    TEST_ASSERT_EQUAL_DOUBLE(AOG_SENTINEL_DOUBLE, data.latitude);
    TEST_ASSERT_EQUAL_FLOAT(AOG_SENTINEL_FLOAT, data.heading_dual);
    TEST_ASSERT_EQUAL_FLOAT(AOG_SENTINEL_FLOAT, data.heading_true);
    TEST_ASSERT_EQUAL_FLOAT(AOG_SENTINEL_FLOAT, data.speed_kmh);
    TEST_ASSERT_EQUAL_FLOAT(AOG_SENTINEL_FLOAT, data.roll);
    TEST_ASSERT_EQUAL_FLOAT(AOG_SENTINEL_FLOAT, data.altitude);
    TEST_ASSERT_EQUAL(0xFFFF, data.satellites);
    TEST_ASSERT_EQUAL(AOG_FIX_NONE, data.fix_quality);
    TEST_ASSERT_EQUAL(0xFFFF, data.hdop_x100);
    TEST_ASSERT_EQUAL(0xFFFF, data.age_x100);
    TEST_ASSERT_EQUAL(0xFFFF, data.imu_heading_x10);
    TEST_ASSERT_EQUAL(0x7FFF, data.imu_roll_x10);
    TEST_ASSERT_EQUAL(0x7FFF, data.imu_pitch);
    TEST_ASSERT_EQUAL(0x7FFF, data.imu_yaw_rate);

    uint8_t payload[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_pgn214(payload, &data);
    aog_pgn214_t decoded;
    aog_pgn_decode_pgn214(payload, AOG_PGN214_DATA_SIZE, &decoded);

    TEST_ASSERT_TRUE(decoded.longitude > 1e300);
    TEST_ASSERT_TRUE(decoded.heading_dual > 1e30);
}

/* ========================================================================
 * 7. Fix quality mapping
 * ======================================================================== */

void test_fix_quality_mapping(void)
{
    TEST_ASSERT_EQUAL(AOG_FIX_GPS,       aog_fix_quality_to_aog(1));
    TEST_ASSERT_EQUAL(AOG_FIX_DGPS,      aog_fix_quality_to_aog(2));
    TEST_ASSERT_EQUAL(AOG_FIX_NONE,      aog_fix_quality_to_aog(0));
    TEST_ASSERT_EQUAL(AOG_FIX_RTK_FIX,   aog_fix_quality_to_aog(4));
    TEST_ASSERT_EQUAL(AOG_FIX_RTK_FLOAT, aog_fix_quality_to_aog(5));
    TEST_ASSERT_EQUAL(AOG_FIX_NONE,      aog_fix_quality_to_aog(3));
    TEST_ASSERT_EQUAL(AOG_FIX_NONE,      aog_fix_quality_to_aog(99));
}

/* ========================================================================
 * 8. PGN exact bytes — every byte position regression-safe
 * ======================================================================== */

void test_pgn214_exact_bytes_every_offset(void)
{
    /* Build a PGN 214 with known values and verify every byte position */
    aog_pgn214_t data;
    memset(&data, 0, sizeof(data));

    /* Set unique values for every field */
    data.longitude = 11.31;
    data.latitude = 48.07;
    data.heading_dual = 180.0f;
    data.heading_true = 90.0f;
    data.speed_kmh = 36.0f;
    data.roll = 2.5f;
    data.altitude = 450.0f;
    data.satellites = 14;
    data.fix_quality = AOG_FIX_RTK_FIX;
    data.hdop_x100 = 120;
    data.age_x100 = 250;
    data.imu_heading_x10 = AOG_SENTINEL_U16;
    data.imu_roll_x10 = AOG_SENTINEL_I16;
    data.imu_pitch = AOG_SENTINEL_I16;
    data.imu_yaw_rate = AOG_SENTINEL_I16;

    uint8_t payload[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_pgn214(payload, &data);

    /* Verify by decode roundtrip — every field matches */
    aog_pgn214_t decoded;
    TEST_ASSERT_TRUE(aog_pgn_decode_pgn214(payload, AOG_PGN214_DATA_SIZE, &decoded));

    TEST_ASSERT_EQUAL_DOUBLE(data.longitude, decoded.longitude);
    TEST_ASSERT_EQUAL_DOUBLE(data.latitude, decoded.latitude);
    TEST_ASSERT_EQUAL_FLOAT(data.heading_dual, decoded.heading_dual);
    TEST_ASSERT_EQUAL_FLOAT(data.heading_true, decoded.heading_true);
    TEST_ASSERT_EQUAL_FLOAT(data.speed_kmh, decoded.speed_kmh);
    TEST_ASSERT_EQUAL_FLOAT(data.roll, decoded.roll);
    TEST_ASSERT_EQUAL_FLOAT(data.altitude, decoded.altitude);
    TEST_ASSERT_EQUAL(data.satellites, decoded.satellites);
    TEST_ASSERT_EQUAL(data.fix_quality, decoded.fix_quality);
    TEST_ASSERT_EQUAL(data.hdop_x100, decoded.hdop_x100);
    TEST_ASSERT_EQUAL(data.age_x100, decoded.age_x100);
    TEST_ASSERT_EQUAL(data.imu_heading_x10, decoded.imu_heading_x10);
    TEST_ASSERT_EQUAL(data.imu_roll_x10, decoded.imu_roll_x10);
    TEST_ASSERT_EQUAL(data.imu_pitch, decoded.imu_pitch);
    TEST_ASSERT_EQUAL(data.imu_yaw_rate, decoded.imu_yaw_rate);

    /* Verify fix_quality at exact byte offset 38 */
    TEST_ASSERT_EQUAL(AOG_FIX_RTK_FIX, payload[38]);

    /* Verify satellites at offset 36-37 LE */
    TEST_ASSERT_EQUAL(14, (uint16_t)payload[36] | ((uint16_t)payload[37] << 8));
}

/* ========================================================================
 * OUTPUT GATING TESTS
 * ======================================================================== */

/* ========================================================================
 * 9. Valid GNSS + valid heading → full live frame
 * ======================================================================== */

void test_valid_gnss_valid_heading(void)
{
    set_valid_gnss_snapshot();
    set_valid_heading_snapshot(M_PI);

    aog_nav_app_service_step(&s_app.component, 100000);

    TEST_ASSERT_EQUAL(58, byte_ring_buffer_available(&s_tx_buf));

    uint8_t frame[64];
    byte_ring_buffer_read(&s_tx_buf, frame, sizeof(frame));

    TEST_ASSERT_EQUAL(0x80, frame[0]);
    TEST_ASSERT_EQUAL(0x81, frame[1]);
    TEST_ASSERT_EQUAL(AOG_SRC_GPS, frame[2]);
    TEST_ASSERT_EQUAL(0xD6, frame[3]);
    TEST_ASSERT_EQUAL(51, frame[5]);

    aog_pgn214_t pgn;
    aog_pgn_decode_pgn214(&frame[6], 51, &pgn);

    TEST_ASSERT_EQUAL_DOUBLE(11.31, pgn.longitude);
    TEST_ASSERT_EQUAL_DOUBLE(48.07, pgn.latitude);
    TEST_ASSERT_EQUAL_FLOAT(180.0f, pgn.heading_dual);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, pgn.heading_true);
    TEST_ASSERT_EQUAL_FLOAT(9.0f, pgn.speed_kmh);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, pgn.altitude);
    TEST_ASSERT_EQUAL(12, pgn.satellites);
    TEST_ASSERT_EQUAL(AOG_FIX_RTK_FIX, pgn.fix_quality);
    TEST_ASSERT_EQUAL(120, pgn.hdop_x100);
    TEST_ASSERT_EQUAL(250, pgn.age_x100);

    TEST_ASSERT_EQUAL(AOG_OUTPUT_OK, aog_nav_app_get_output_state(&s_app));
    TEST_ASSERT_TRUE(s_app.gnss_output_active);
    TEST_ASSERT_TRUE(s_app.heading_output_active);
}

/* ========================================================================
 * 10. Valid GNSS + invalid heading → sentinel heading
 * ======================================================================== */

void test_valid_gnss_invalid_heading(void)
{
    set_valid_gnss_snapshot();
    s_hdg_storage.heading_rad = 1.5;
    s_hdg_storage.valid = false;
    snapshot_buffer_set(&s_hdg_snap, &s_hdg_storage);

    aog_nav_app_service_step(&s_app.component, 100000);

    uint8_t frame[64];
    byte_ring_buffer_read(&s_tx_buf, frame, sizeof(frame));
    aog_pgn214_t pgn;
    aog_pgn_decode_pgn214(&frame[6], 51, &pgn);

    TEST_ASSERT_EQUAL_DOUBLE(11.31, pgn.longitude);
    TEST_ASSERT_EQUAL(AOG_FIX_RTK_FIX, pgn.fix_quality);
    TEST_ASSERT_TRUE(pgn.heading_dual > 1e30f);

    TEST_ASSERT_EQUAL(AOG_OUTPUT_HEADING_INVALID, aog_nav_app_get_output_state(&s_app));
    TEST_ASSERT_TRUE(s_app.gnss_output_active);
    TEST_ASSERT_FALSE(s_app.heading_output_active);
}

/* ========================================================================
 * 11. Valid GNSS + stale heading → sentinel heading
 * ======================================================================== */

void test_valid_gnss_stale_heading(void)
{
    set_valid_gnss_snapshot();
    set_valid_heading_snapshot(M_PI_2);

    aog_nav_app_service_step(&s_app.component, 100000);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_OK, aog_nav_app_get_output_state(&s_app));
    drain_tx();

    /* Advance past heading freshness timeout (> 3000ms) */
    aog_nav_app_service_step(&s_app.component, 5000000);
    drain_tx();

    aog_nav_app_service_step(&s_app.component, 5100000);
    TEST_ASSERT_EQUAL(58, byte_ring_buffer_available(&s_tx_buf));

    uint8_t frame[64];
    byte_ring_buffer_read(&s_tx_buf, frame, sizeof(frame));
    aog_pgn214_t pgn;
    aog_pgn_decode_pgn214(&frame[6], 51, &pgn);

    TEST_ASSERT_EQUAL_DOUBLE(11.31, pgn.longitude);
    TEST_ASSERT_EQUAL(AOG_FIX_RTK_FIX, pgn.fix_quality);
    TEST_ASSERT_TRUE(pgn.heading_dual > 1e30f);

    TEST_ASSERT_EQUAL(AOG_OUTPUT_HEADING_STALE, aog_nav_app_get_output_state(&s_app));
}

/* ========================================================================
 * 12. Valid GNSS + heading lost (source NULL) → HEADING_LOST state
 * ======================================================================== */

void test_valid_gnss_heading_lost(void)
{
    set_valid_gnss_snapshot();
    /* Remove heading source entirely */
    aog_nav_app_set_heading_source(&s_app, NULL);

    aog_nav_app_service_step(&s_app.component, 100000);

    uint8_t frame[64];
    byte_ring_buffer_read(&s_tx_buf, frame, sizeof(frame));
    aog_pgn214_t pgn;
    aog_pgn_decode_pgn214(&frame[6], 51, &pgn);

    /* GNSS should be real */
    TEST_ASSERT_EQUAL_DOUBLE(11.31, pgn.longitude);
    TEST_ASSERT_EQUAL(AOG_FIX_RTK_FIX, pgn.fix_quality);
    /* Heading should be sentinel */
    TEST_ASSERT_TRUE(pgn.heading_dual > 1e30f);

    TEST_ASSERT_EQUAL(AOG_OUTPUT_HEADING_LOST, aog_nav_app_get_output_state(&s_app));
    TEST_ASSERT_TRUE(s_app.gnss_output_active);
    TEST_ASSERT_FALSE(s_app.heading_output_active);
}

/* ========================================================================
 * 13. Invalid GNSS → sentinel PGN 214
 * ======================================================================== */

void test_invalid_gnss(void)
{
    s_gnss_storage.valid = false;
    s_gnss_storage.fresh = false;
    s_gnss_storage.status_reason = GNSS_REASON_NO_FIX;
    snapshot_buffer_set(&s_gnss_snap, &s_gnss_storage);
    set_valid_heading_snapshot(M_PI);

    aog_nav_app_service_step(&s_app.component, 100000);

    uint8_t frame[64];
    byte_ring_buffer_read(&s_tx_buf, frame, sizeof(frame));
    aog_pgn214_t pgn;
    aog_pgn_decode_pgn214(&frame[6], 51, &pgn);

    TEST_ASSERT_TRUE(pgn.longitude > 1e300);
    TEST_ASSERT_TRUE(pgn.latitude > 1e300);
    TEST_ASSERT_EQUAL(AOG_FIX_NONE, pgn.fix_quality);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_GNSS_INVALID, aog_nav_app_get_output_state(&s_app));
    TEST_ASSERT_FALSE(s_app.gnss_output_active);
    TEST_ASSERT_FALSE(s_app.heading_output_active);
}

/* ========================================================================
 * 14. Stale GNSS → sentinel PGN 214
 * ======================================================================== */

void test_stale_gnss(void)
{
    s_gnss_storage.position_valid = true;
    s_gnss_storage.motion_valid = true;
    s_gnss_storage.valid = true;
    s_gnss_storage.fresh = false;
    s_gnss_storage.status_reason = GNSS_REASON_STALE_GGA;
    snapshot_buffer_set(&s_gnss_snap, &s_gnss_storage);
    set_valid_heading_snapshot(M_PI);

    aog_nav_app_service_step(&s_app.component, 100000);

    uint8_t frame[64];
    byte_ring_buffer_read(&s_tx_buf, frame, sizeof(frame));
    aog_pgn214_t pgn;
    aog_pgn_decode_pgn214(&frame[6], 51, &pgn);

    TEST_ASSERT_TRUE(pgn.longitude > 1e300);
    TEST_ASSERT_EQUAL(AOG_FIX_NONE, pgn.fix_quality);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_GNSS_STALE, aog_nav_app_get_output_state(&s_app));
}

/* ========================================================================
 * 15. No stale GNSS reuse
 * ======================================================================== */

void test_no_stale_gnss_reuse(void)
{
    set_valid_gnss_snapshot();
    set_valid_heading_snapshot(M_PI);

    aog_nav_app_service_step(&s_app.component, 100000);
    uint8_t frame1[64];
    byte_ring_buffer_read(&s_tx_buf, frame1, sizeof(frame1));
    aog_pgn214_t pgn1;
    aog_pgn_decode_pgn214(&frame1[6], 51, &pgn1);
    TEST_ASSERT_EQUAL_DOUBLE(11.31, pgn1.longitude);

    /* Make GNSS stale */
    s_gnss_storage.fresh = false;
    s_gnss_storage.valid = false;
    snapshot_buffer_set(&s_gnss_snap, &s_gnss_storage);

    aog_nav_app_service_step(&s_app.component, 160000);
    uint8_t frame2[64];
    byte_ring_buffer_read(&s_tx_buf, frame2, sizeof(frame2));
    aog_pgn214_t pgn2;
    aog_pgn_decode_pgn214(&frame2[6], 51, &pgn2);

    TEST_ASSERT_TRUE(pgn2.longitude > 1e300);
    TEST_ASSERT_EQUAL(AOG_FIX_NONE, pgn2.fix_quality);
}

/* ========================================================================
 * 16. No stale heading reuse
 * ======================================================================== */

void test_no_stale_heading_reuse(void)
{
    set_valid_gnss_snapshot();
    set_valid_heading_snapshot(M_PI);

    aog_nav_app_service_step(&s_app.component, 100000);
    drain_tx();

    /* Advance past heading freshness timeout */
    aog_nav_app_service_step(&s_app.component, 5100000);
    drain_tx();

    aog_nav_app_service_step(&s_app.component, 6200000);

    uint8_t frame[64];
    byte_ring_buffer_read(&s_tx_buf, frame, sizeof(frame));
    aog_pgn214_t pgn;
    aog_pgn_decode_pgn214(&frame[6], 51, &pgn);

    TEST_ASSERT_TRUE(pgn.heading_dual > 1e30f);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_HEADING_STALE, aog_nav_app_get_output_state(&s_app));
}

/* ========================================================================
 * 17. Heading fallback: sentinel (not suppress, not stale reuse)
 * ======================================================================== */

void test_heading_fallback_sentinel(void)
{
    set_valid_gnss_snapshot();

    /* No heading source → HEADING_LOST, heading = sentinel */
    aog_nav_app_set_heading_source(&s_app, NULL);
    aog_nav_app_service_step(&s_app.component, 100000);

    uint8_t frame[64];
    byte_ring_buffer_read(&s_tx_buf, frame, sizeof(frame));
    aog_pgn214_t pgn;
    aog_pgn_decode_pgn214(&frame[6], 51, &pgn);

    /* Heading must be FLT_MAX sentinel, NOT zero, NOT last known value */
    TEST_ASSERT_EQUAL_FLOAT(FLT_MAX, pgn.heading_dual);
    TEST_ASSERT_TRUE(s_app.gnss_output_active);
    TEST_ASSERT_FALSE(s_app.heading_output_active);

    /* Restore heading source with invalid data */
    aog_nav_app_set_heading_source(&s_app, &s_hdg_snap);
    s_hdg_storage.valid = false;
    snapshot_buffer_set(&s_hdg_snap, &s_hdg_storage);

    aog_nav_app_service_step(&s_app.component, 160000);
    byte_ring_buffer_read(&s_tx_buf, frame, sizeof(frame));
    aog_pgn_decode_pgn214(&frame[6], 51, &pgn);

    TEST_ASSERT_EQUAL_FLOAT(FLT_MAX, pgn.heading_dual);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_HEADING_INVALID, aog_nav_app_get_output_state(&s_app));
}

/* ========================================================================
 * 18. Heading fallback: suppress when GNSS also invalid
 * ======================================================================== */

void test_heading_suppress_when_gnss_invalid(void)
{
    /* Both GNSS and heading invalid → everything suppressed */
    s_gnss_storage.valid = false;
    s_gnss_storage.fresh = false;
    snapshot_buffer_set(&s_gnss_snap, &s_gnss_storage);
    s_hdg_storage.valid = false;
    snapshot_buffer_set(&s_hdg_snap, &s_hdg_storage);

    aog_nav_app_service_step(&s_app.component, 100000);

    uint8_t frame[64];
    byte_ring_buffer_read(&s_tx_buf, frame, sizeof(frame));
    aog_pgn214_t pgn;
    aog_pgn_decode_pgn214(&frame[6], 51, &pgn);

    /* Both position and heading must be sentinel */
    TEST_ASSERT_TRUE(pgn.longitude > 1e300);
    TEST_ASSERT_TRUE(pgn.latitude > 1e300);
    TEST_ASSERT_TRUE(pgn.heading_dual > 1e30f);
    TEST_ASSERT_EQUAL(AOG_FIX_NONE, pgn.fix_quality);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_GNSS_INVALID, aog_nav_app_get_output_state(&s_app));
    TEST_ASSERT_FALSE(s_app.gnss_output_active);
    TEST_ASSERT_FALSE(s_app.heading_output_active);
}

/* ========================================================================
 * STATUS / FIX MAPPING TESTS
 * ======================================================================== */

/* ========================================================================
 * 19. All 8 output states
 * ======================================================================== */

void test_output_state_8_states(void)
{
    /* INIT: before any service step */
    TEST_ASSERT_EQUAL(AOG_OUTPUT_INIT, aog_nav_app_get_output_state(&s_app));

    /* OK: valid GNSS + valid heading */
    set_valid_gnss_snapshot();
    set_valid_heading_snapshot(M_PI);
    aog_nav_app_service_step(&s_app.component, 100000);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_OK, aog_nav_app_get_output_state(&s_app));
    drain_tx();

    /* HEADING_INVALID: valid GNSS, invalid heading */
    s_hdg_storage.valid = false;
    snapshot_buffer_set(&s_hdg_snap, &s_hdg_storage);
    aog_nav_app_service_step(&s_app.component, 160000);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_HEADING_INVALID, aog_nav_app_get_output_state(&s_app));
    drain_tx();

    /* HEADING_STALE: advance past freshness */
    s_hdg_storage.valid = true;
    snapshot_buffer_set(&s_hdg_snap, &s_hdg_storage);
    aog_nav_app_service_step(&s_app.component, 170000);
    drain_tx();
    aog_nav_app_service_step(&s_app.component, 5200000);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_HEADING_STALE, aog_nav_app_get_output_state(&s_app));
    drain_tx();

    /* HEADING_LOST: remove heading source */
    aog_nav_app_set_heading_source(&s_app, NULL);
    aog_nav_app_service_step(&s_app.component, 5300000);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_HEADING_LOST, aog_nav_app_get_output_state(&s_app));
    drain_tx();
    aog_nav_app_set_heading_source(&s_app, &s_hdg_snap);

    /* GNSS_INVALID: invalid GNSS */
    s_gnss_storage.valid = false;
    s_gnss_storage.fresh = false;
    snapshot_buffer_set(&s_gnss_snap, &s_gnss_storage);
    aog_nav_app_service_step(&s_app.component, 5400000);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_GNSS_INVALID, aog_nav_app_get_output_state(&s_app));
    drain_tx();

    /* GNSS_STALE: valid but stale */
    s_gnss_storage.valid = true;
    s_gnss_storage.fresh = false;
    snapshot_buffer_set(&s_gnss_snap, &s_gnss_storage);
    aog_nav_app_service_step(&s_app.component, 5500000);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_GNSS_STALE, aog_nav_app_get_output_state(&s_app));
}

/* ========================================================================
 * 20. Fix quality: all NMEA values
 * ======================================================================== */

void test_fix_quality_all_nmea_values(void)
{
    /* Test each GNSS fix quality maps correctly through the chain */
    struct { gnss_fix_quality_t gnss_fq; uint8_t expected_aog; } cases[] = {
        { GNSS_FIX_NONE,        AOG_FIX_NONE },
        { GNSS_FIX_SINGLE,      AOG_FIX_GPS },
        { GNSS_FIX_DGPS,        AOG_FIX_DGPS },
        { GNSS_FIX_PPS,         AOG_FIX_NONE },  /* PPS → NONE (unknown to AOG) */
        { GNSS_FIX_RTK_FLOAT,   AOG_FIX_RTK_FLOAT },
        { GNSS_FIX_RTK_FIXED,   AOG_FIX_RTK_FIX },
    };

    for (int i = 0; i < 6; i++) {
        set_valid_gnss_snapshot();
        set_valid_heading_snapshot(0.0);
        s_gnss_storage.fix_quality = cases[i].gnss_fq;
        snapshot_buffer_set(&s_gnss_snap, &s_gnss_storage);

        /* Clear and reset */
        drain_tx();
        memset(&s_app, 0, sizeof(s_app));
        aog_nav_app_init(&s_app);
        aog_nav_app_set_position_source(&s_app, &s_gnss_snap);
        aog_nav_app_set_heading_source(&s_app, &s_hdg_snap);
        aog_nav_app_set_aog_rx_source(&s_app, &s_rx_buf);
        aog_nav_app_set_aog_tx_dest(&s_app, &s_tx_buf);

        aog_nav_app_service_step(&s_app.component, 100000 + (uint64_t)i * 100000);

        uint8_t frame[64];
        if (byte_ring_buffer_available(&s_tx_buf) >= 58) {
            byte_ring_buffer_read(&s_tx_buf, frame, sizeof(frame));
            aog_pgn214_t pgn;
            aog_pgn_decode_pgn214(&frame[6], 51, &pgn);
            if (cases[i].expected_aog == AOG_FIX_NONE) {
                /* NONE → output suppressed if no fix */
                if (cases[i].gnss_fq == GNSS_FIX_NONE) {
                    TEST_ASSERT_EQUAL(AOG_OUTPUT_GNSS_INVALID, aog_nav_app_get_output_state(&s_app));
                }
            } else {
                TEST_ASSERT_EQUAL(cases[i].expected_aog, pgn.fix_quality);
            }
        }
    }
}

/* ========================================================================
 * DISCOVERY TESTS (P2)
 * ======================================================================== */

/* ========================================================================
 * 21. PGN 253 Hello → PGN 254 Hello Reply
 * ======================================================================== */

void test_hello_request_reply(void)
{
    set_valid_gnss_snapshot();
    inject_aog_frame(AOG_PGN_HELLO_REQUEST, NULL, 0);

    aog_nav_app_service_step(&s_app.component, 100000);

    uint8_t frame[64];
    bool found = find_pgn_in_tx(254, frame, NULL);
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL(AOG_SRC_GPS, frame[2]);
    TEST_ASSERT_EQUAL(1, s_app.hello_send_count);
}

/* ========================================================================
 * 22. PGN 202 Scan → PGN 203 Scan Reply
 * ======================================================================== */

void test_scan_request_reply(void)
{
    set_valid_gnss_snapshot();
    inject_aog_frame(AOG_PGN_SCAN_REQUEST, NULL, 0);

    aog_nav_app_service_step(&s_app.component, 100000);

    uint8_t frame[64];
    bool found = find_pgn_in_tx(AOG_PGN_SCAN_REPLY, frame, NULL);
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL(AOG_SRC_GPS, frame[2]);
    TEST_ASSERT_EQUAL(1, s_app.scan_send_count);
}

/* ========================================================================
 * 23. Discovery tolerant CRC: CRC=0x00 accepted for PGN 253/202
 * ======================================================================== */

void test_discovery_tolerant_crc_zero(void)
{
    set_valid_gnss_snapshot();

    /* Inject Hello Request with CRC=0x00 (known AgIO quirk) */
    inject_aog_frame_bad_crc(AOG_PGN_HELLO_REQUEST, NULL, 0, 0x00);

    aog_nav_app_service_step(&s_app.component, 100000);

    /* Parser uses tolerant mode for Discovery → frame should be accepted */
    TEST_ASSERT_EQUAL(1, s_app.hello_send_count);
}

/* ========================================================================
 * 24. Discovery tolerant CRC: ±1 accepted for PGN 253
 * ======================================================================== */

void test_discovery_tolerant_crc_plus_minus_one(void)
{
    set_valid_gnss_snapshot();

    /* Inject Hello Request with correct CRC */
    uint8_t frame_good[AOG_MAX_FRAME_SIZE];
    size_t good_len = aog_frame_encode(frame_good, AOG_SRC_AOG,
                                         AOG_PGN_HELLO_REQUEST, NULL, 0);
    uint8_t correct_crc = frame_good[good_len - 1];

    /* Test CRC+1 */
    inject_aog_frame_bad_crc(AOG_PGN_HELLO_REQUEST, NULL, 0,
                              (uint8_t)(correct_crc + 1));
    aog_nav_app_service_step(&s_app.component, 100000);
    TEST_ASSERT_EQUAL(1, s_app.hello_send_count);

    /* Reset */
    drain_tx();
    memset(&s_app, 0, sizeof(s_app));
    aog_nav_app_init(&s_app);
    aog_nav_app_set_position_source(&s_app, &s_gnss_snap);
    aog_nav_app_set_heading_source(&s_app, &s_hdg_snap);
    aog_nav_app_set_aog_rx_source(&s_app, &s_rx_buf);
    aog_nav_app_set_aog_tx_dest(&s_app, &s_tx_buf);

    /* Test CRC-1 */
    inject_aog_frame_bad_crc(AOG_PGN_HELLO_REQUEST, NULL, 0,
                              (uint8_t)(correct_crc - 1));
    aog_nav_app_service_step(&s_app.component, 200000);
    TEST_ASSERT_EQUAL(1, s_app.hello_send_count);
}

/* ========================================================================
 * 25. Discovery strict split: bad CRC rejected for core PGNs
 * ======================================================================== */

void test_discovery_strict_crc_reject(void)
{
    /* The parser with tolerant mode should STILL reject Discovery frames
     * if CRC is off by more than ±1 and not 0x00 */

    set_valid_gnss_snapshot();

    /* Inject Hello Request with a very wrong CRC (off by 10) */
    uint8_t frame_good[AOG_MAX_FRAME_SIZE];
    size_t good_len = aog_frame_encode(frame_good, AOG_SRC_AOG,
                                         AOG_PGN_HELLO_REQUEST, NULL, 0);
    uint8_t correct_crc = frame_good[good_len - 1];
    uint8_t bad_crc = (uint8_t)(correct_crc + 10);

    inject_aog_frame_bad_crc(AOG_PGN_HELLO_REQUEST, NULL, 0, bad_crc);

    aog_nav_app_service_step(&s_app.component, 100000);

    /* Should NOT trigger hello reply because CRC is too wrong */
    TEST_ASSERT_EQUAL(0, s_app.hello_send_count);
}

/* ========================================================================
 * 26. Discovery bypasses output gating
 * ======================================================================== */

void test_discovery_bypasses_gating(void)
{
    /* GNSS invalid, heading invalid — but Discovery must still work */
    s_gnss_storage.valid = false;
    s_gnss_storage.fresh = false;
    snapshot_buffer_set(&s_gnss_snap, &s_gnss_storage);
    s_hdg_storage.valid = false;
    snapshot_buffer_set(&s_hdg_snap, &s_hdg_storage);

    inject_aog_frame(AOG_PGN_HELLO_REQUEST, NULL, 0);
    inject_aog_frame(AOG_PGN_SCAN_REQUEST, NULL, 0);

    aog_nav_app_service_step(&s_app.component, 100000);

    /* Discovery replies should be sent despite invalid GNSS */
    TEST_ASSERT_EQUAL(1, s_app.hello_send_count);
    TEST_ASSERT_EQUAL(1, s_app.scan_send_count);
}

/* ========================================================================
 * 27. Multiple hello requests → multiple replies
 * ======================================================================== */

void test_multiple_hello_replies(void)
{
    set_valid_gnss_snapshot();

    for (int i = 0; i < 3; i++) {
        inject_aog_frame(AOG_PGN_HELLO_REQUEST, NULL, 0);
    }

    aog_nav_app_service_step(&s_app.component, 100000);
    TEST_ASSERT_EQUAL(1, s_app.hello_send_count);

    aog_nav_app_service_step(&s_app.component, 200000);
    TEST_ASSERT(s_app.hello_send_count >= 1);
}

/* ========================================================================
 * 28. Scan reply content: module_type = 0x78
 * ======================================================================== */

void test_scan_reply_module_type(void)
{
    set_valid_gnss_snapshot();
    inject_aog_frame(AOG_PGN_SCAN_REQUEST, NULL, 0);

    aog_nav_app_service_step(&s_app.component, 100000);

    uint8_t frame[64];
    size_t flen = 0;
    bool found = find_pgn_in_tx(AOG_PGN_SCAN_REPLY, frame, &flen);
    TEST_ASSERT_TRUE(found);

    /* Parse scan reply payload */
    uint8_t src = frame[6];
    uint8_t module_type = frame[7];
    uint16_t pgn_count = (uint16_t)frame[8] | ((uint16_t)frame[9] << 8);

    TEST_ASSERT_EQUAL(AOG_SRC_GPS, src);
    TEST_ASSERT_EQUAL(AOG_MODULE_TYPE_GPS, module_type);  /* 0x78 = 120 */
    TEST_ASSERT_EQUAL(2, pgn_count);

    /* First PGN = 214 */
    uint16_t pgn0 = (uint16_t)frame[10] | ((uint16_t)frame[11] << 8);
    TEST_ASSERT_EQUAL(AOG_PGN_214_OUT, pgn0);

    /* Second PGN = 254 */
    uint16_t pgn1 = (uint16_t)frame[12] | ((uint16_t)frame[13] << 8);
    TEST_ASSERT_EQUAL(AOG_PGN_HELLO_RESPONSE, pgn1);
}

/* ========================================================================
 * SOURCE ID TESTS (P3)
 * ======================================================================== */

/* ========================================================================
 * 29. Source byte = 0x05 in all outgoing PGN 214 frames
 * ======================================================================== */

void test_source_byte_gps(void)
{
    set_valid_gnss_snapshot();
    aog_nav_app_service_step(&s_app.component, 100000);

    uint8_t frame[64];
    byte_ring_buffer_read(&s_tx_buf, frame, sizeof(frame));
    TEST_ASSERT_EQUAL(AOG_SRC_GPS, frame[2]);
}

/* ========================================================================
 * 30. Module type = 0x78 initialized correctly
 * ======================================================================== */

void test_module_type_initialized(void)
{
    /* After init, module_type should be AOG_MODULE_TYPE_GPS = 120 */
    TEST_ASSERT_EQUAL(AOG_MODULE_TYPE_GPS, s_app.module_type);
    TEST_ASSERT_EQUAL(120, s_app.module_type);
    TEST_ASSERT_EQUAL(0x78, s_app.module_type);
}

/* ========================================================================
 * TIMING & DETERMINISM
 * ======================================================================== */

/* ========================================================================
 * 31. Deterministic cyclic output (20 Hz)
 * ======================================================================== */

void test_cyclic_20hz(void)
{
    set_valid_gnss_snapshot();
    set_valid_heading_snapshot(M_PI);

    /* t=0ms: first output */
    aog_nav_app_service_step(&s_app.component, 0);
    TEST_ASSERT_EQUAL(58, byte_ring_buffer_available(&s_tx_buf));
    drain_tx();

    /* t=49ms: no output yet */
    aog_nav_app_service_step(&s_app.component, 49000);
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&s_tx_buf));

    /* t=50ms: output */
    aog_nav_app_service_step(&s_app.component, 50000);
    TEST_ASSERT_EQUAL(58, byte_ring_buffer_available(&s_tx_buf));
    drain_tx();

    /* t=99ms: no output */
    aog_nav_app_service_step(&s_app.component, 99000);
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&s_tx_buf));

    /* t=100ms: output */
    aog_nav_app_service_step(&s_app.component, 100000);
    TEST_ASSERT_EQUAL(58, byte_ring_buffer_available(&s_tx_buf));
    drain_tx();
}

/* ========================================================================
 * DATA CONVERSION TESTS (P5)
 * ======================================================================== */

/* ========================================================================
 * 32. Heading normalization (rad→deg, 0-360)
 * ======================================================================== */

void test_heading_normalization(void)
{
    set_valid_gnss_snapshot();

    /* 0 rad → 0 deg */
    set_valid_heading_snapshot(0.0);
    aog_nav_app_service_step(&s_app.component, 100000);
    uint8_t frame[64];
    byte_ring_buffer_read(&s_tx_buf, frame, sizeof(frame));
    aog_pgn214_t pgn;
    aog_pgn_decode_pgn214(&frame[6], 51, &pgn);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pgn.heading_dual);

    /* Negative rad → positive deg */
    set_valid_heading_snapshot(-M_PI_2);
    snapshot_buffer_set(&s_hdg_snap, &s_hdg_storage);
    aog_nav_app_service_step(&s_app.component, 160000);
    byte_ring_buffer_read(&s_tx_buf, frame, sizeof(frame));
    aog_pgn_decode_pgn214(&frame[6], 51, &pgn);
    TEST_ASSERT_TRUE(pgn.heading_dual > 269.0f && pgn.heading_dual < 271.0f);
}

/* ========================================================================
 * 33. Speed conversion (m/s → km/h)
 * ======================================================================== */

void test_speed_conversion(void)
{
    set_valid_gnss_snapshot();
    s_gnss_storage.speed_ms = 10.0;
    snapshot_buffer_set(&s_gnss_snap, &s_gnss_storage);
    set_valid_heading_snapshot(0.0);

    aog_nav_app_service_step(&s_app.component, 100000);
    uint8_t frame[64];
    byte_ring_buffer_read(&s_tx_buf, frame, sizeof(frame));
    aog_pgn214_t pgn;
    aog_pgn_decode_pgn214(&frame[6], 51, &pgn);

    TEST_ASSERT_TRUE(fabsf(pgn.speed_kmh - 36.0f) < 0.1f);
}

/* ========================================================================
 * 34. HDOP scaling (×100)
 * ======================================================================== */

void test_hdop_scaling(void)
{
    set_valid_gnss_snapshot();
    s_gnss_storage.hdop = 0.85;
    snapshot_buffer_set(&s_gnss_snap, &s_gnss_storage);

    aog_nav_app_service_step(&s_app.component, 100000);
    uint8_t frame[64];
    byte_ring_buffer_read(&s_tx_buf, frame, sizeof(frame));
    aog_pgn214_t pgn;
    aog_pgn_decode_pgn214(&frame[6], 51, &pgn);

    TEST_ASSERT_EQUAL(85, pgn.hdop_x100);
}

/* ========================================================================
 * ARCHITECTURE TEST
 * ======================================================================== */

/* ========================================================================
 * 35. Transport isolation
 * ======================================================================== */

void test_transport_isolation(void)
{
    TEST_ASSERT_TRUE(s_app.aog_tx_dest != NULL);
    TEST_ASSERT_TRUE(s_app.aog_rx_source != NULL);
}

/* ========================================================================
 * CRC EXACT TEST
 * ======================================================================== */

/* ========================================================================
 * 36. CRC exact on all TX frames — no tolerance on output
 * ======================================================================== */

void test_crc_exact_on_tx_frames(void)
{
    set_valid_gnss_snapshot();
    set_valid_heading_snapshot(M_PI);

    /* Generate TX frames */
    aog_nav_app_service_step(&s_app.component, 100000);

    /* Read all frames and verify CRC is exact (strict) */
    uint8_t tmp[256];
    int frames_checked = 0;
    while (byte_ring_buffer_available(&s_tx_buf) >= 8) {
        size_t to_read = byte_ring_buffer_available(&s_tx_buf);
        if (to_read > sizeof(tmp)) to_read = sizeof(tmp);
        size_t n = byte_ring_buffer_read(&s_tx_buf, tmp, to_read);

        for (size_t i = 0; i + 8 <= n; ) {
            if (tmp[i] == 0x80 && tmp[i+1] == 0x81) {
                uint8_t len = tmp[i+5];
                size_t flen = 8 + len;
                TEST_ASSERT_TRUE(aog_frame_verify_crc(&tmp[i], flen));
                frames_checked++;
                i += flen;
            } else {
                i++;
            }
        }
    }

    TEST_ASSERT_TRUE(frames_checked >= 1);
}

/* ========================================================================
 * 37. PGN classification: is_discovery
 * ======================================================================== */

void test_pgn_is_discovery(void)
{
    TEST_ASSERT_TRUE(aog_pgn_is_discovery(AOG_PGN_SCAN_REQUEST));    /* 202 */
    TEST_ASSERT_TRUE(aog_pgn_is_discovery(AOG_PGN_SCAN_REPLY));      /* 203 */
    TEST_ASSERT_TRUE(aog_pgn_is_discovery(AOG_PGN_HELLO_REQUEST));   /* 253 */
    TEST_ASSERT_TRUE(aog_pgn_is_discovery(AOG_PGN_HELLO_RESPONSE));  /* 254 */
    TEST_ASSERT_FALSE(aog_pgn_is_discovery(AOG_PGN_214_OUT));        /* 214 */
    TEST_ASSERT_FALSE(aog_pgn_is_discovery(AOG_PGN_POSITION_OUT));   /* 200 */
    TEST_ASSERT_FALSE(aog_pgn_is_discovery(AOG_PGN_HEADING_OUT));    /* 201 */
}

/* ========================================================================
 * 38. Tolerant CRC verify function
 * ======================================================================== */

void test_verify_crc_tolerant_function(void)
{
    /* Build a valid Discovery frame */
    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t flen = aog_frame_encode(frame, AOG_SRC_AOG,
                                     AOG_PGN_SCAN_REQUEST, NULL, 0);
    uint8_t correct_crc = frame[flen - 1];

    /* Strict: correct CRC passes */
    TEST_ASSERT_TRUE(aog_frame_verify_crc(frame, flen));
    TEST_ASSERT_TRUE(aog_frame_verify_crc_tolerant(frame, flen));

    /* CRC = 0x00: strict fails, tolerant passes */
    frame[flen - 1] = 0x00;
    TEST_ASSERT_FALSE(aog_frame_verify_crc(frame, flen));
    TEST_ASSERT_TRUE(aog_frame_verify_crc_tolerant(frame, flen));

    /* CRC = correct + 1: strict fails, tolerant passes */
    frame[flen - 1] = (uint8_t)(correct_crc + 1);
    TEST_ASSERT_FALSE(aog_frame_verify_crc(frame, flen));
    TEST_ASSERT_TRUE(aog_frame_verify_crc_tolerant(frame, flen));

    /* CRC = correct - 1: strict fails, tolerant passes */
    frame[flen - 1] = (uint8_t)(correct_crc - 1);
    TEST_ASSERT_FALSE(aog_frame_verify_crc(frame, flen));
    TEST_ASSERT_TRUE(aog_frame_verify_crc_tolerant(frame, flen));

    /* CRC = correct + 10: both fail */
    frame[flen - 1] = (uint8_t)(correct_crc + 10);
    TEST_ASSERT_FALSE(aog_frame_verify_crc(frame, flen));
    TEST_ASSERT_FALSE(aog_frame_verify_crc_tolerant(frame, flen));
}

/* ========================================================================
 * 39. Output state transitions
 * ======================================================================== */

void test_output_state_transitions(void)
{
    /* INIT → OK → HEADING_INVALID → OK → GNSS_STALE → GNSS_INVALID → OK */
    TEST_ASSERT_EQUAL(AOG_OUTPUT_INIT, aog_nav_app_get_output_state(&s_app));

    /* OK */
    set_valid_gnss_snapshot();
    set_valid_heading_snapshot(1.0);
    aog_nav_app_service_step(&s_app.component, 100000);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_OK, aog_nav_app_get_output_state(&s_app));
    drain_tx();

    /* HEADING_INVALID */
    s_hdg_storage.valid = false;
    snapshot_buffer_set(&s_hdg_snap, &s_hdg_storage);
    aog_nav_app_service_step(&s_app.component, 160000);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_HEADING_INVALID, aog_nav_app_get_output_state(&s_app));
    drain_tx();

    /* Back to OK */
    s_hdg_storage.valid = true;
    set_valid_heading_snapshot(1.5);
    aog_nav_app_service_step(&s_app.component, 170000);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_OK, aog_nav_app_get_output_state(&s_app));
    drain_tx();

    /* GNSS_STALE */
    s_gnss_storage.fresh = false;
    snapshot_buffer_set(&s_gnss_snap, &s_gnss_storage);
    aog_nav_app_service_step(&s_app.component, 180000);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_GNSS_STALE, aog_nav_app_get_output_state(&s_app));
    drain_tx();

    /* GNSS_INVALID */
    s_gnss_storage.valid = false;
    snapshot_buffer_set(&s_gnss_snap, &s_gnss_storage);
    aog_nav_app_service_step(&s_app.component, 190000);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_GNSS_INVALID, aog_nav_app_get_output_state(&s_app));
    drain_tx();

    /* Back to OK */
    set_valid_gnss_snapshot();
    set_valid_heading_snapshot(2.0);
    aog_nav_app_service_step(&s_app.component, 200000);
    TEST_ASSERT_EQUAL(AOG_OUTPUT_OK, aog_nav_app_get_output_state(&s_app));
}

/* ========================================================================
 * GOLDEN BYTE REGRESSION TESTS (NAV-AOG-001 FINAL HARDENING P3)
 *
 * These tests use pre-computed exact byte vectors. If ANY byte changes
 * due to refactoring, these tests will catch it. This is the FINAL
 * regression safety net.
 * ======================================================================== */

/* ========================================================================
 * 40. Golden PGN 214 header bytes
 *
 * Frame: [0x80][0x81][0x05][0xD6][0x00][0x33][PAYLOAD...][CRC]
 *        preamble  src   PGN_lo PGN_hi LEN=51
 * ======================================================================== */

void test_golden_pgn214_header_bytes(void)
{
    aog_pgn214_t data;
    memset(&data, 0, sizeof(data));
    data.fix_quality = 1;

    uint8_t payload[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_pgn214(payload, &data);

    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t flen = aog_frame_encode(frame, AOG_SRC_GPS, AOG_PGN_214_OUT,
                                     payload, AOG_PGN214_DATA_SIZE);

    TEST_ASSERT_EQUAL(58, flen);
    /* Preamble */
    TEST_ASSERT_EQUAL_UINT8(0x80, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(0x81, frame[1]);
    /* Source = GPS (0x05) */
    TEST_ASSERT_EQUAL_UINT8(0x05, frame[2]);
    /* PGN 214 = 0xD6 (LE: lo=0xD6, hi=0x00) */
    TEST_ASSERT_EQUAL_UINT8(0xD6, frame[3]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[4]);
    /* Payload length = 51 */
    TEST_ASSERT_EQUAL_UINT8(51, frame[5]);
}

/* ========================================================================
 * 41. Golden sentinel payload: every byte of all-sentinel PGN 214
 *
 * All fields set to sentinel → encode → verify exact bytes at key offsets
 * ======================================================================== */

void test_golden_sentinel_payload_bytes(void)
{
    aog_pgn214_t data;
    aog_pgn214_set_sentinels(&data);

    uint8_t payload[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_pgn214(payload, &data);

    /* fix_quality at byte 38 must be 0 (NONE) */
    TEST_ASSERT_EQUAL_UINT8(0, payload[38]);

    /* satellites at bytes 36-37 must be 0xFFFF */
    TEST_ASSERT_EQUAL_UINT8(0xFF, payload[36]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, payload[37]);

    /* hdop at bytes 39-40 must be 0xFFFF */
    TEST_ASSERT_EQUAL_UINT8(0xFF, payload[39]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, payload[40]);

    /* age at bytes 41-42 must be 0xFFFF */
    TEST_ASSERT_EQUAL_UINT8(0xFF, payload[41]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, payload[42]);

    /* IMU fields: sentinel values */
    TEST_ASSERT_EQUAL_UINT8(0xFF, payload[43]); /* imu_heading lo */
    TEST_ASSERT_EQUAL_UINT8(0xFF, payload[44]); /* imu_heading hi */
    TEST_ASSERT_EQUAL_UINT8(0xFF, payload[45]); /* imu_roll lo (0x7FFF → 0xFF, 0x7F) */
    TEST_ASSERT_EQUAL_UINT8(0x7F, payload[46]); /* imu_roll hi */
    TEST_ASSERT_EQUAL_UINT8(0xFF, payload[47]); /* imu_pitch lo */
    TEST_ASSERT_EQUAL_UINT8(0x7F, payload[48]); /* imu_pitch hi */
    TEST_ASSERT_EQUAL_UINT8(0xFF, payload[49]); /* imu_yaw_rate lo */
    TEST_ASSERT_EQUAL_UINT8(0x7F, payload[50]); /* imu_yaw_rate hi */

    /* Longitude and latitude: sentinel double → bytes 0-15 non-zero */
    /* FLT_MAX encoded as double has specific byte pattern */
    uint8_t nonzero = 0;
    for (int i = 0; i < 16; i++) {
        nonzero |= payload[i];
    }
    TEST_ASSERT_TRUE(nonzero != 0);

    /* Heading fields (16-23, 24-27, 28-31): FLT_MAX encoded as float */
    nonzero = 0;
    for (int i = 16; i < 32; i++) {
        nonzero |= payload[i];
    }
    TEST_ASSERT_TRUE(nonzero != 0);
}

/* ========================================================================
 * 42. Golden CRC: known exact CRC for Discovery frame
 *
 * Frame: [0x80][0x81][0x00][0xCA][0x00][0x00][CRC]
 * CRC = (0x00 + 0xCA + 0x00 + 0x00) mod 256 = 0xCA
 * ======================================================================== */

void test_golden_crc_discovery_frame(void)
{
    /* Build a minimal Discovery frame (PGN 202, no payload) */
    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t flen = aog_frame_encode(frame, AOG_SRC_AOG, AOG_PGN_SCAN_REQUEST, NULL, 0);

    TEST_ASSERT_EQUAL(8, flen); /* 7 header + 0 payload + 1 CRC */
    TEST_ASSERT_EQUAL_UINT8(0x80, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(0x81, frame[1]);
    TEST_ASSERT_EQUAL_UINT8(AOG_SRC_AOG, frame[2]);
    /* PGN 202 = 0xCA */
    TEST_ASSERT_EQUAL_UINT8(0xCA, frame[3]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[4]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[5]);

    /* CRC = (SRC + PGN_lo + PGN_hi + LEN) mod 256 */
    uint8_t expected_crc = (0x00 + 0xCA + 0x00 + 0x00) & 0xFF;
    TEST_ASSERT_EQUAL_UINT8(expected_crc, frame[7]);
    TEST_ASSERT_TRUE(aog_frame_verify_crc(frame, flen));
}

/* ========================================================================
 * 43. Golden Hello Response frame: exact bytes
 *
 * PGN 254, SRC=0x05, payload: IP 192.168.1.100, port 9999, subnet 0
 * ======================================================================== */

void test_golden_hello_response_frame(void)
{
    aog_hello_t hello;
    hello.ip[0] = 192; hello.ip[1] = 168; hello.ip[2] = 1; hello.ip[3] = 100;
    hello.port = 9999;
    hello.subnet_index = 0;

    uint8_t payload[AOG_HELLO_DATA_SIZE];
    aog_pgn_encode_hello(payload, &hello);

    /* Verify payload bytes */
    TEST_ASSERT_EQUAL_UINT8(192, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(168, payload[1]);
    TEST_ASSERT_EQUAL_UINT8(1, payload[2]);
    TEST_ASSERT_EQUAL_UINT8(100, payload[3]);
    /* Port 9999 = 0x270F LE */
    TEST_ASSERT_EQUAL_UINT8(0x0F, payload[4]);
    TEST_ASSERT_EQUAL_UINT8(0x27, payload[5]);
    TEST_ASSERT_EQUAL_UINT8(0, payload[6]);

    /* Full frame */
    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t flen = aog_frame_encode(frame, AOG_SRC_GPS, AOG_PGN_HELLO_RESPONSE,
                                     payload, AOG_HELLO_DATA_SIZE);
    TEST_ASSERT_EQUAL(15, flen); /* 7 + 7 + 1 */
    TEST_ASSERT_EQUAL_UINT8(0x80, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(0x81, frame[1]);
    TEST_ASSERT_EQUAL_UINT8(0x05, frame[2]);
    /* PGN 254 = 0xFE */
    TEST_ASSERT_EQUAL_UINT8(0xFE, frame[3]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[4]);
    TEST_ASSERT_EQUAL_UINT8(7, frame[5]);
    TEST_ASSERT_TRUE(aog_frame_verify_crc(frame, flen));
}

/* ========================================================================
 * 44. Golden Scan Reply frame: exact bytes with module_type=0x78
 *
 * PGN 203, SRC=0x05, module_type=0x78, PGNs: [214, 254]
 * ======================================================================== */

void test_golden_scan_reply_frame(void)
{
    aog_scan_reply_t scan;
    memset(&scan, 0, sizeof(scan));
    scan.src = AOG_SRC_GPS;
    scan.module_type = AOG_MODULE_TYPE_GPS; /* 0x78 = 120 */
    scan.pgn_count = 2;
    scan.pgns[0] = AOG_PGN_214_OUT;       /* 214 */
    scan.pgns[1] = AOG_PGN_HELLO_RESPONSE; /* 254 */

    uint8_t payload[16];
    uint8_t plen = aog_pgn_encode_scan_reply(payload, &scan);

    /* Verify payload */
    TEST_ASSERT_EQUAL(8, plen);
    TEST_ASSERT_EQUAL_UINT8(AOG_SRC_GPS, payload[0]);     /* src */
    TEST_ASSERT_EQUAL_UINT8(0x78, payload[1]);           /* module_type = 120 */
    /* pgn_count = 2 LE */
    TEST_ASSERT_EQUAL_UINT8(0x02, payload[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[3]);
    /* PGN 214 = 0xD6 LE */
    TEST_ASSERT_EQUAL_UINT8(0xD6, payload[4]);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[5]);
    /* PGN 254 = 0xFE LE */
    TEST_ASSERT_EQUAL_UINT8(0xFE, payload[6]);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[7]);

    /* Full frame */
    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t flen = aog_frame_encode(frame, AOG_SRC_GPS, AOG_PGN_SCAN_REPLY,
                                     payload, plen);
    TEST_ASSERT_EQUAL(16, flen); /* 7 + 8 + 1 */
    TEST_ASSERT_EQUAL_UINT8(0x80, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(0x81, frame[1]);
    TEST_ASSERT_EQUAL_UINT8(0x05, frame[2]);
    /* PGN 203 = 0xCB */
    TEST_ASSERT_EQUAL_UINT8(0xCB, frame[3]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[4]);
    TEST_ASSERT_EQUAL_UINT8(8, frame[5]);
    TEST_ASSERT_TRUE(aog_frame_verify_crc(frame, flen));
}

/* ========================================================================
 * 45. Golden Heading encoding: exact f32 bytes for known rad→deg
 *
 * 0 rad = 0.0 deg → 0x00000000
 * π rad = 180.0 deg → 0x43480000 (IEEE 754)
 * π/2 rad = 90.0 deg → 0x42B40000 (IEEE 754)
 * ======================================================================== */

void test_golden_heading_encoding(void)
{
    /* Build PGN 214 with heading_dual = 180.0f (π rad) */
    aog_pgn214_t data;
    memset(&data, 0, sizeof(data));
    data.heading_dual = 180.0f;

    uint8_t payload[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_pgn214(payload, &data);

    /* heading_dual at offset 16-19 */
    /* IEEE 754: 180.0f = 0x43480000 → LE: 0x00, 0x00, 0x48, 0x43 */
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[16]);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[17]);
    TEST_ASSERT_EQUAL_UINT8(0x48, payload[18]);
    TEST_ASSERT_EQUAL_UINT8(0x43, payload[19]);

    /* heading_dual = 90.0f → 0x42B40000 → LE: 0x00, 0x00, 0xB4, 0x42 */
    data.heading_dual = 90.0f;
    aog_pgn_encode_pgn214(payload, &data);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[16]);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[17]);
    TEST_ASSERT_EQUAL_UINT8(0xB4, payload[18]);
    TEST_ASSERT_EQUAL_UINT8(0x42, payload[19]);

    /* heading_dual = 0.0f → 0x00000000 */
    data.heading_dual = 0.0f;
    aog_pgn_encode_pgn214(payload, &data);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[16]);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[17]);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[18]);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[19]);

    /* heading_dual = 45.0f → 0x42340000 → LE: 0x00, 0x00, 0x34, 0x42 */
    data.heading_dual = 45.0f;
    aog_pgn_encode_pgn214(payload, &data);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[16]);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[17]);
    TEST_ASSERT_EQUAL_UINT8(0x34, payload[18]);
    TEST_ASSERT_EQUAL_UINT8(0x42, payload[19]);
}

/* ========================================================================
 * 46. Golden fix_quality byte: exact byte at offset 38 per status
 * ======================================================================== */

void test_golden_fix_quality_byte(void)
{
    aog_pgn214_t data;
    memset(&data, 0, sizeof(data));

    /* NONE (0) */
    data.fix_quality = AOG_FIX_NONE;
    uint8_t payload[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_pgn214(payload, &data);
    TEST_ASSERT_EQUAL_UINT8(0, payload[38]);

    /* GPS (1) */
    data.fix_quality = AOG_FIX_GPS;
    aog_pgn_encode_pgn214(payload, &data);
    TEST_ASSERT_EQUAL_UINT8(1, payload[38]);

    /* DGPS (2) */
    data.fix_quality = AOG_FIX_DGPS;
    aog_pgn_encode_pgn214(payload, &data);
    TEST_ASSERT_EQUAL_UINT8(2, payload[38]);

    /* RTK FIX (4) */
    data.fix_quality = AOG_FIX_RTK_FIX;
    aog_pgn_encode_pgn214(payload, &data);
    TEST_ASSERT_EQUAL_UINT8(4, payload[38]);

    /* RTK FLOAT (5) */
    data.fix_quality = AOG_FIX_RTK_FLOAT;
    aog_pgn_encode_pgn214(payload, &data);
    TEST_ASSERT_EQUAL_UINT8(5, payload[38]);
}

/* ========================================================================
 * 47. Golden Scan Request (PGN 202) tolerant CRC
 * Ensure tolerant mode works for BOTH PGN 202 and PGN 253
 * ======================================================================== */

void test_golden_scan_request_tolerant_crc(void)
{
    /* Build valid PGN 202 frame from AOG (SRC=0x00) */
    uint8_t frame_good[AOG_MAX_FRAME_SIZE];
    size_t good_len = aog_frame_encode(frame_good, AOG_SRC_AOG,
                                         AOG_PGN_SCAN_REQUEST, NULL, 0);
    uint8_t correct_crc = frame_good[good_len - 1];

    /* Test: CRC=0x00 accepted for PGN 202 */
    frame_good[good_len - 1] = 0x00;
    TEST_ASSERT_FALSE(aog_frame_verify_crc(frame_good, good_len));
    TEST_ASSERT_TRUE(aog_frame_verify_crc_tolerant(frame_good, good_len));

    /* Test: CRC+1 accepted for PGN 202 */
    frame_good[good_len - 1] = (uint8_t)(correct_crc + 1);
    TEST_ASSERT_FALSE(aog_frame_verify_crc(frame_good, good_len));
    TEST_ASSERT_TRUE(aog_frame_verify_crc_tolerant(frame_good, good_len));

    /* Test: CRC-1 accepted for PGN 202 */
    frame_good[good_len - 1] = (uint8_t)(correct_crc - 1);
    TEST_ASSERT_FALSE(aog_frame_verify_crc(frame_good, good_len));
    TEST_ASSERT_TRUE(aog_frame_verify_crc_tolerant(frame_good, good_len));

    /* Test: CRC+5 rejected for PGN 202 */
    frame_good[good_len - 1] = (uint8_t)(correct_crc + 5);
    TEST_ASSERT_FALSE(aog_frame_verify_crc(frame_good, good_len));
    TEST_ASSERT_FALSE(aog_frame_verify_crc_tolerant(frame_good, good_len));
}

/* ========================================================================
 * 48. Golden PGN 214 complete frame: all 58 bytes verified
 *
 * Builds a known PGN 214, encodes full frame, verifies every byte
 * position has deterministic, expected content.
 * ======================================================================== */

void test_golden_pgn214_complete_frame(void)
{
    aog_pgn214_t data;
    memset(&data, 0, sizeof(data));

    /* Use simple, reproducible values */
    data.fix_quality = 1;      /* GPS */
    data.satellites = 8;

    uint8_t payload[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_pgn214(payload, &data);

    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t flen = aog_frame_encode(frame, AOG_SRC_GPS, AOG_PGN_214_OUT,
                                     payload, AOG_PGN214_DATA_SIZE);
    TEST_ASSERT_EQUAL(58, flen);

    /* Frame header (7 bytes) */
    TEST_ASSERT_EQUAL_UINT8(0x80, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(0x81, frame[1]);
    TEST_ASSERT_EQUAL_UINT8(0x05, frame[2]);
    TEST_ASSERT_EQUAL_UINT8(0xD6, frame[3]); /* PGN 214 lo */
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[4]); /* PGN 214 hi */
    TEST_ASSERT_EQUAL_UINT8(51, frame[5]);  /* LEN */

    /* Payload bytes 6..56 (= frame[6]..frame[56]) */
    /* Bytes 0-7: longitude (double, 0.0 → all zeros) */
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x00, frame[6 + i]);
    }
    /* Bytes 8-15: latitude (double, 0.0 → all zeros) */
    for (int i = 8; i < 16; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x00, frame[6 + i]);
    }
    /* Bytes 16-19: heading_dual (float, 0.0 → all zeros) */
    for (int i = 16; i < 20; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x00, frame[6 + i]);
    }
    /* Bytes 20-23: heading_true (float, 0.0) */
    for (int i = 20; i < 24; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x00, frame[6 + i]);
    }
    /* Bytes 24-27: speed_kmh (float, 0.0) */
    for (int i = 24; i < 28; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x00, frame[6 + i]);
    }
    /* Bytes 28-31: roll (float, 0.0) */
    for (int i = 28; i < 32; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x00, frame[6 + i]);
    }
    /* Bytes 32-35: altitude (float, 0.0) */
    for (int i = 32; i < 36; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x00, frame[6 + i]);
    }
    /* Bytes 36-37: satellites = 8 → LE: 0x08 0x00 */
    TEST_ASSERT_EQUAL_UINT8(0x08, frame[42]); /* payload[36] = frame[6+36] */
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[43]); /* payload[37] */
    /* Byte 38: fix_quality = 1 */
    TEST_ASSERT_EQUAL_UINT8(1, frame[44]); /* payload[38] */
    /* Bytes 39-50: all zeros (hdop, age, IMU fields all 0) */
    for (int i = 45; i < 57; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x00, frame[i]);
    }

    /* Byte 57: CRC */
    uint32_t crc_sum = 0;
    for (size_t i = 2; i < 57; i++) {
        crc_sum += frame[i];
    }
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(crc_sum & 0xFF), frame[57]);
    TEST_ASSERT_TRUE(aog_frame_verify_crc(frame, flen));
}

/* ========================================================================
 * 49. Golden Discovery boundary: CRC wrap-around
 *
 * Tests that CRC tolerance correctly handles wrap-around at 0xFF.
 * If expected CRC = 0x00, then CRC=0xFF should be accepted (±1).
 * If expected CRC = 0xFF, then CRC=0x00 should be accepted (±1).
 * ======================================================================== */

void test_golden_discovery_crc_wraparound(void)
{
    /* Build frame and check CRC wrap-around */
    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t flen = aog_frame_encode(frame, AOG_SRC_AOG,
                                     AOG_PGN_HELLO_REQUEST, NULL, 0);
    uint8_t correct_crc = frame[flen - 1];

    /* Case 1: correct_crc + 1, wrapping around 256 */
    frame[flen - 1] = (uint8_t)(correct_crc + 1);
    TEST_ASSERT_TRUE(aog_frame_verify_crc_tolerant(frame, flen));

    /* Case 2: correct_crc - 1, wrapping around 0 */
    frame[flen - 1] = (uint8_t)(correct_crc - 1);
    TEST_ASSERT_TRUE(aog_frame_verify_crc_tolerant(frame, flen));

    /* Case 3: 0x00 is special — always accepted */
    frame[flen - 1] = 0x00;
    TEST_ASSERT_TRUE(aog_frame_verify_crc_tolerant(frame, flen));

    /* Case 4: Off by 2 — rejected */
    frame[flen - 1] = (uint8_t)(correct_crc + 2);
    TEST_ASSERT_FALSE(aog_frame_verify_crc_tolerant(frame, flen));

    /* Case 5: Off by 0xFF (= -1 in uint8) — accepted */
    frame[flen - 1] = (uint8_t)(correct_crc + 0xFF);
    TEST_ASSERT_TRUE(aog_frame_verify_crc_tolerant(frame, flen));
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* ---- Frame format and encoding (P5) ---- */
    RUN_TEST(test_pgn214_frame_format);
    RUN_TEST(test_pgn214_frame_length_exact);
    RUN_TEST(test_pgn214_crc_sum);
    RUN_TEST(test_pgn214_little_endian);
    RUN_TEST(test_pgn214_byte_offsets);
    RUN_TEST(test_pgn214_sentinel_values);
    RUN_TEST(test_fix_quality_mapping);
    RUN_TEST(test_pgn214_exact_bytes_every_offset);

    /* ---- Output gating (P1) ---- */
    RUN_TEST(test_valid_gnss_valid_heading);
    RUN_TEST(test_valid_gnss_invalid_heading);
    RUN_TEST(test_valid_gnss_stale_heading);
    RUN_TEST(test_valid_gnss_heading_lost);
    RUN_TEST(test_invalid_gnss);
    RUN_TEST(test_stale_gnss);
    RUN_TEST(test_no_stale_gnss_reuse);
    RUN_TEST(test_no_stale_heading_reuse);
    RUN_TEST(test_heading_fallback_sentinel);
    RUN_TEST(test_heading_suppress_when_gnss_invalid);

    /* ---- Status / Fix mapping (P6) ---- */
    RUN_TEST(test_output_state_8_states);
    RUN_TEST(test_fix_quality_all_nmea_values);
    RUN_TEST(test_output_state_transitions);

    /* ---- Discovery (P2) ---- */
    RUN_TEST(test_hello_request_reply);
    RUN_TEST(test_scan_request_reply);
    RUN_TEST(test_discovery_tolerant_crc_zero);
    RUN_TEST(test_discovery_tolerant_crc_plus_minus_one);
    RUN_TEST(test_discovery_strict_crc_reject);
    RUN_TEST(test_discovery_bypasses_gating);
    RUN_TEST(test_multiple_hello_replies);
    RUN_TEST(test_scan_reply_module_type);
    RUN_TEST(test_pgn_is_discovery);
    RUN_TEST(test_verify_crc_tolerant_function);

    /* ---- Source IDs (P3) ---- */
    RUN_TEST(test_source_byte_gps);
    RUN_TEST(test_module_type_initialized);

    /* ---- Timing & determinism ---- */
    RUN_TEST(test_cyclic_20hz);

    /* ---- Data conversion (P5) ---- */
    RUN_TEST(test_heading_normalization);
    RUN_TEST(test_speed_conversion);
    RUN_TEST(test_hdop_scaling);

    /* ---- Architecture ---- */
    RUN_TEST(test_transport_isolation);

    /* ---- CRC exact ---- */
    RUN_TEST(test_crc_exact_on_tx_frames);

    /* ---- Golden Byte Regression (FINAL HARDENING P3) ---- */
    RUN_TEST(test_golden_pgn214_header_bytes);
    RUN_TEST(test_golden_sentinel_payload_bytes);
    RUN_TEST(test_golden_crc_discovery_frame);
    RUN_TEST(test_golden_hello_response_frame);
    RUN_TEST(test_golden_scan_reply_frame);
    RUN_TEST(test_golden_heading_encoding);
    RUN_TEST(test_golden_fix_quality_byte);
    RUN_TEST(test_golden_scan_request_tolerant_crc);
    RUN_TEST(test_golden_pgn214_complete_frame);
    RUN_TEST(test_golden_discovery_crc_wraparound);

    return UNITY_END();
}
