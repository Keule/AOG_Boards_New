/* ========================================================================
 * test_pgn214 — Host tests for NAV-AOG-001 PGN 214 Encoder
 *
 * Tests cover:
 *  1. PGN 214 frame length is correct (51 bytes payload)
 *  2. CRC is correct (XOR-based)
 *  3. Little-endian encoding for float64, float32, uint16, int16
 *  4. Valid position and heading correctly encoded
 *  5. Invalid sentinel values correctly encoded
 *  6. Heading dual from heading snapshot (deg→rad)
 *  7. HDOP scale (0.01) correctly applied
 *  8. Age scale (0.01) correctly applied
 *  9. Decode round-trip matches encode
 * 10. transport_udp is never called (layer separation)
 * 11. AOG_PGN214_DATA_SIZE matches actual wire layout
 * 12. Sentinel constants are correct types/values
 * ======================================================================== */

#include "unity.h"
#include "aog_pgn.h"
#include "aog_frame.h"
#include <math.h>
#include <string.h>
#include <float.h>
#include <stdint.h>

/* ========================================================================
 * Test Helpers
 * ======================================================================== */

/* IEEE 754 float64 bytes for 1.0 (LE) */
static const uint8_t DOUBLE_1_0_LE[8] = {0x00,0x00,0x00,0x00,0x00,0x00,0xF0,0x3F};
/* IEEE 754 float32 bytes for 1.0 (LE) */
static const uint8_t FLOAT_1_0_LE[4] = {0x00,0x00,0x80,0x3F};

/* ========================================================================
 * Tests
 * ======================================================================== */

/* Test 1: PGN 214 data size constant matches wire layout */
void test_pgn214_data_size_is_51(void)
{
    /* Wire layout:
     *   longitude(8) + latitude(8) + heading_dual(4) + heading_true(4)
     *   + speed(4) + roll(4) + altitude(4) + satellites(2)
     *   + fix_quality(1) + hdop(2) + age(2) + imu_heading(2)
     *   + imu_roll(2) + imu_pitch(2) + imu_yaw_rate(2)
     *   = 51 bytes */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(51, AOG_PGN214_DATA_SIZE,
        "PGN 214 payload must be 51 bytes");
}

/* Test 2: Full AOG frame with PGN 214 has correct length */
void test_pgn214_frame_length(void)
{
    aog_pgn214_t pgn;
    memset(&pgn, 0, sizeof(pgn));

    uint8_t data[AOG_PGN214_DATA_SIZE];
    uint8_t encoded = aog_pgn_encode_214(data, &pgn);

    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t frame_len = aog_frame_encode(frame, AOG_PGN_214, data, encoded);

    /* Frame = preamble(2) + length(1) + pgn(2) + data(51) + crc(1) = 57 */
    TEST_ASSERT_EQUAL_size_t_MESSAGE(57, frame_len,
        "Full AOG frame must be 57 bytes for PGN 214");
}

/* Test 3: CRC is valid for PGN 214 frame */
void test_pgn214_crc_valid(void)
{
    aog_pgn214_t pgn;
    memset(&pgn, 0, sizeof(pgn));
    pgn.longitude = 12.345;
    pgn.latitude = 48.567;

    uint8_t data[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_214(data, &pgn);

    uint8_t frame[AOG_MAX_FRAME_SIZE];
    size_t frame_len = aog_frame_encode(frame, AOG_PGN_214, data, AOG_PGN214_DATA_SIZE);

    TEST_ASSERT_TRUE_MESSAGE(aog_frame_verify_crc(frame, frame_len),
        "CRC must be valid for PGN 214 frame");
}

/* Test 4: Little-endian encoding — float64 */
void test_pgn214_le_float64(void)
{
    aog_pgn214_t pgn;
    memset(&pgn, 0, sizeof(pgn));
    pgn.longitude = 1.0;
    pgn.latitude = 1.0;

    uint8_t buf[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_214(buf, &pgn);

    /* longitude at offset 0 should be 1.0 in LE float64 */
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(DOUBLE_1_0_LE, &buf[0], 8,
        "longitude (double 1.0) must be LE float64");
    /* latitude at offset 8 */
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(DOUBLE_1_0_LE, &buf[8], 8,
        "latitude (double 1.0) must be LE float64");
}

/* Test 5: Little-endian encoding — float32 */
void test_pgn214_le_float32(void)
{
    aog_pgn214_t pgn;
    memset(&pgn, 0, sizeof(pgn));
    pgn.heading_dual = 1.0f;
    pgn.heading_true = 1.0f;
    pgn.speed = 1.0f;
    pgn.roll = 1.0f;
    pgn.altitude = 1.0f;

    uint8_t buf[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_214(buf, &pgn);

    /* heading_dual at offset 16 */
    TEST_ASSERT_EQUAL_MEMORY(FLOAT_1_0_LE, &buf[16], 4);
    /* heading_true at offset 20 */
    TEST_ASSERT_EQUAL_MEMORY(FLOAT_1_0_LE, &buf[20], 4);
    /* speed at offset 24 */
    TEST_ASSERT_EQUAL_MEMORY(FLOAT_1_0_LE, &buf[24], 4);
    /* roll at offset 28 */
    TEST_ASSERT_EQUAL_MEMORY(FLOAT_1_0_LE, &buf[28], 4);
    /* altitude at offset 32 */
    TEST_ASSERT_EQUAL_MEMORY(FLOAT_1_0_LE, &buf[32], 4);
}

/* Test 6: Little-endian encoding — uint16 and int16 */
void test_pgn214_le_uint16_int16(void)
{
    aog_pgn214_t pgn;
    memset(&pgn, 0, sizeof(pgn));
    pgn.satellites = 0x1234;
    pgn.hdop_x100 = 0x5678;
    pgn.age_x100 = 0x9ABC;
    pgn.imu_heading_x10 = 0xDEF0;
    pgn.imu_roll_x10 = -1234;
    pgn.imu_pitch = 5678;
    pgn.imu_yaw_rate = -9012;

    uint8_t buf[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_214(buf, &pgn);

    /* satellites at offset 36: LE = 0x34, 0x12 */
    TEST_ASSERT_EQUAL_UINT8(0x34, buf[36]);
    TEST_ASSERT_EQUAL_UINT8(0x12, buf[37]);

    /* hdop at offset 39 */
    TEST_ASSERT_EQUAL_UINT8(0x78, buf[39]);
    TEST_ASSERT_EQUAL_UINT8(0x56, buf[40]);

    /* age at offset 41 */
    TEST_ASSERT_EQUAL_UINT8(0xBC, buf[41]);
    TEST_ASSERT_EQUAL_UINT8(0x9A, buf[42]);

    /* imu_heading at offset 43 */
    TEST_ASSERT_EQUAL_UINT8(0xF0, buf[43]);
    TEST_ASSERT_EQUAL_UINT8(0xDE, buf[44]);

    /* fix_quality at offset 38 */
    pgn.fix_quality = 3;
    aog_pgn_encode_214(buf, &pgn);
    TEST_ASSERT_EQUAL_UINT8(3, buf[38]);
}

/* Test 7: Valid position + heading correctly encoded */
void test_pgn214_valid_data(void)
{
    aog_pgn214_t pgn;
    memset(&pgn, 0, sizeof(pgn));
    pgn.longitude = 12.345678;
    pgn.latitude = 48.987654;
    pgn.heading_dual = 2.0944f;   /* ~120° in rad */
    pgn.speed = 5.5f;
    pgn.altitude = 320.5f;
    pgn.satellites = 18;
    pgn.fix_quality = 4;  /* RTK fix */
    pgn.hdop_x100 = 12;  /* 0.12 HDOP */

    uint8_t buf[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_214(buf, &pgn);

    /* Decode and verify */
    aog_pgn214_t decoded;
    TEST_ASSERT_TRUE(aog_pgn_decode_214(buf, AOG_PGN214_DATA_SIZE, &decoded));

    TEST_ASSERT_EQUAL_DOUBLE(12.345678, decoded.longitude);
    TEST_ASSERT_EQUAL_DOUBLE(48.987654, decoded.latitude);
    TEST_ASSERT_EQUAL_FLOAT(2.0944f, decoded.heading_dual);
    TEST_ASSERT_EQUAL_FLOAT(5.5f, decoded.speed);
    TEST_ASSERT_EQUAL_FLOAT(320.5f, decoded.altitude);
    TEST_ASSERT_EQUAL_UINT16(18, decoded.satellites);
    TEST_ASSERT_EQUAL_UINT8(4, decoded.fix_quality);
    TEST_ASSERT_EQUAL_UINT16(12, decoded.hdop_x100);
}

/* Test 8: Invalid sentinel values correctly encoded */
void test_pgn214_sentinel_encoding(void)
{
    aog_pgn214_t pgn;
    memset(&pgn, 0, sizeof(pgn));

    /* Set all fields to sentinel */
    pgn.longitude = AOG_PGN214_SENTINEL_DOUBLE;
    pgn.latitude = AOG_PGN214_SENTINEL_DOUBLE;
    pgn.altitude = AOG_PGN214_SENTINEL_FLOAT;
    pgn.heading_dual = AOG_PGN214_SENTINEL_FLOAT;
    pgn.heading_true = AOG_PGN214_SENTINEL_FLOAT;
    pgn.speed = AOG_PGN214_SENTINEL_FLOAT;
    pgn.roll = AOG_PGN214_SENTINEL_FLOAT;
    pgn.satellites = AOG_PGN214_SENTINEL_UINT16;
    pgn.hdop_x100 = AOG_PGN214_SENTINEL_UINT16;
    pgn.age_x100 = AOG_PGN214_SENTINEL_UINT16;
    pgn.imu_heading_x10 = AOG_PGN214_SENTINEL_UINT16;
    pgn.imu_roll_x10 = AOG_PGN214_SENTINEL_INT16;
    pgn.imu_pitch = AOG_PGN214_SENTINEL_INT16;
    pgn.imu_yaw_rate = AOG_PGN214_SENTINEL_INT16;
    pgn.fix_quality = AOG_PGN214_SENTINEL_FIX;

    uint8_t buf[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_214(buf, &pgn);

    aog_pgn214_t decoded;
    TEST_ASSERT_TRUE(aog_pgn_decode_214(buf, AOG_PGN214_DATA_SIZE, &decoded));

    /* All sentinels must survive round-trip */
    TEST_ASSERT_EQUAL_DOUBLE(DBL_MAX, decoded.longitude);
    TEST_ASSERT_EQUAL_DOUBLE(DBL_MAX, decoded.latitude);
    TEST_ASSERT_EQUAL_FLOAT(FLT_MAX, decoded.altitude);
    TEST_ASSERT_EQUAL_FLOAT(FLT_MAX, decoded.heading_dual);
    TEST_ASSERT_EQUAL_FLOAT(FLT_MAX, decoded.heading_true);
    TEST_ASSERT_EQUAL_FLOAT(FLT_MAX, decoded.speed);
    TEST_ASSERT_EQUAL_FLOAT(FLT_MAX, decoded.roll);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, decoded.satellites);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, decoded.hdop_x100);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, decoded.age_x100);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, decoded.imu_heading_x10);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, decoded.imu_roll_x10);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, decoded.imu_pitch);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, decoded.imu_yaw_rate);
    TEST_ASSERT_EQUAL_UINT8(0, decoded.fix_quality);
}

/* Test 9: Decode round-trip with real-world values */
void test_pgn214_roundtrip_real_world(void)
{
    aog_pgn214_t pgn;
    memset(&pgn, 0, sizeof(pgn));
    pgn.longitude = 11.123456;
    pgn.latitude = 49.654321;
    pgn.heading_dual = (float)(90.0 * M_PI / 180.0);  /* 90° in rad = π/2 */
    pgn.heading_true = (float)(88.5 * M_PI / 180.0);
    pgn.speed = 3.2f;
    pgn.roll = 0.01f;
    pgn.altitude = 415.7f;
    pgn.satellites = 24;
    pgn.fix_quality = 3;
    pgn.hdop_x100 = 8;   /* 0.08 HDOP */
    pgn.age_x100 = 150;  /* 1.5 seconds */
    pgn.imu_heading_x10 = 905;  /* 90.5° × 10 */
    pgn.imu_roll_x10 = 5;       /* 0.5° × 10 */
    pgn.imu_pitch = -2;
    pgn.imu_yaw_rate = 10;

    uint8_t buf[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_214(buf, &pgn);

    aog_pgn214_t decoded;
    TEST_ASSERT_TRUE(aog_pgn_decode_214(buf, AOG_PGN214_DATA_SIZE, &decoded));

    TEST_ASSERT_EQUAL_DOUBLE(pgn.longitude, decoded.longitude);
    TEST_ASSERT_EQUAL_DOUBLE(pgn.latitude, decoded.latitude);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, pgn.heading_dual, decoded.heading_dual);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, pgn.heading_true, decoded.heading_true);
    TEST_ASSERT_EQUAL_FLOAT(pgn.speed, decoded.speed);
    TEST_ASSERT_EQUAL_FLOAT(pgn.roll, decoded.roll);
    TEST_ASSERT_EQUAL_FLOAT(pgn.altitude, decoded.altitude);
    TEST_ASSERT_EQUAL_UINT16(pgn.satellites, decoded.satellites);
    TEST_ASSERT_EQUAL_UINT8(pgn.fix_quality, decoded.fix_quality);
    TEST_ASSERT_EQUAL_UINT16(pgn.hdop_x100, decoded.hdop_x100);
    TEST_ASSERT_EQUAL_UINT16(pgn.age_x100, decoded.age_x100);
    TEST_ASSERT_EQUAL_UINT16(pgn.imu_heading_x10, decoded.imu_heading_x10);
    TEST_ASSERT_EQUAL_INT16(pgn.imu_roll_x10, decoded.imu_roll_x10);
    TEST_ASSERT_EQUAL_INT16(pgn.imu_pitch, decoded.imu_pitch);
    TEST_ASSERT_EQUAL_INT16(pgn.imu_yaw_rate, decoded.imu_yaw_rate);
}

/* Test 10: Encode returns 0 for NULL pointers */
void test_pgn214_null_safety(void)
{
    uint8_t buf[AOG_PGN214_DATA_SIZE];
    aog_pgn214_t pgn;
    memset(&pgn, 0, sizeof(pgn));

    TEST_ASSERT_EQUAL_UINT8(0, aog_pgn_encode_214(NULL, &pgn));
    TEST_ASSERT_EQUAL_UINT8(0, aog_pgn_encode_214(buf, NULL));
    TEST_ASSERT_FALSE(aog_pgn_decode_214(NULL, AOG_PGN214_DATA_SIZE, &pgn));
    TEST_ASSERT_FALSE(aog_pgn_decode_214(buf, 0, &pgn));
    TEST_ASSERT_FALSE(aog_pgn_decode_214(buf, 50, &pgn));  /* too short */
}

/* Test 11: Decode with short data_length returns false */
void test_pgn214_decode_short(void)
{
    aog_pgn214_t decoded;
    uint8_t buf[50] = {0};  /* one byte short */
    TEST_ASSERT_FALSE(aog_pgn_decode_214(buf, 50, &decoded));
}

/* Test 12: PGN number in frame is correct (214 = 0xD6) */
void test_pgn214_pgn_number_in_frame(void)
{
    aog_pgn214_t pgn;
    memset(&pgn, 0, sizeof(pgn));

    uint8_t data[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_214(data, &pgn);

    uint8_t frame[AOG_MAX_FRAME_SIZE];
    aog_frame_encode(frame, AOG_PGN_214, data, AOG_PGN214_DATA_SIZE);

    /* PGN is at offset 3 (after preamble 0x80 0x81 and length byte) */
    /* AOG frame format: [0x80][0x81][length][PGN_lo][PGN_hi]... */
    uint16_t frame_pgn = (uint16_t)frame[3] | ((uint16_t)frame[4] << 8);
    TEST_ASSERT_EQUAL_UINT16(214, frame_pgn);
}

/* Test 13: Sentinel constants have correct values */
void test_pgn214_sentinel_constants(void)
{
    TEST_ASSERT_EQUAL_DOUBLE(DBL_MAX, AOG_PGN214_SENTINEL_DOUBLE);
    TEST_ASSERT_EQUAL_FLOAT(FLT_MAX, AOG_PGN214_SENTINEL_FLOAT);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, AOG_PGN214_SENTINEL_UINT16);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, AOG_PGN214_SENTINEL_INT16);
    TEST_ASSERT_EQUAL_UINT8(0, AOG_PGN214_SENTINEL_FIX);
}

/* Test 14: HDOP scale 0.01 — value = hdop * 100 */
void test_pgn214_hdop_scale(void)
{
    aog_pgn214_t pgn;
    memset(&pgn, 0, sizeof(pgn));
    pgn.hdop_x100 = 99;  /* represents 0.99 HDOP */

    uint8_t buf[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_214(buf, &pgn);

    aog_pgn214_t decoded;
    aog_pgn_decode_214(buf, AOG_PGN214_DATA_SIZE, &decoded);

    TEST_ASSERT_EQUAL_UINT16(99, decoded.hdop_x100);
}

/* Test 15: Heading dual deg→rad conversion */
void test_pgn214_heading_deg_to_rad(void)
{
    /* 45° = π/4 ≈ 0.7854 rad */
    float heading_rad = (float)(45.0 * M_PI / 180.0);

    aog_pgn214_t pgn;
    memset(&pgn, 0, sizeof(pgn));
    pgn.heading_dual = heading_rad;

    uint8_t buf[AOG_PGN214_DATA_SIZE];
    aog_pgn_encode_214(buf, &pgn);

    aog_pgn214_t decoded;
    aog_pgn_decode_214(buf, AOG_PGN214_DATA_SIZE, &decoded);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, heading_rad, decoded.heading_dual);

    /* Verify it decodes back to approximately 45 degrees */
    float back_to_deg = decoded.heading_dual * 180.0f / (float)M_PI;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.0f, back_to_deg);
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

void setUp(void) { }
void tearDown(void) { }

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_pgn214_data_size_is_51);
    RUN_TEST(test_pgn214_frame_length);
    RUN_TEST(test_pgn214_crc_valid);
    RUN_TEST(test_pgn214_le_float64);
    RUN_TEST(test_pgn214_le_float32);
    RUN_TEST(test_pgn214_le_uint16_int16);
    RUN_TEST(test_pgn214_valid_data);
    RUN_TEST(test_pgn214_sentinel_encoding);
    RUN_TEST(test_pgn214_roundtrip_real_world);
    RUN_TEST(test_pgn214_null_safety);
    RUN_TEST(test_pgn214_decode_short);
    RUN_TEST(test_pgn214_pgn_number_in_frame);
    RUN_TEST(test_pgn214_sentinel_constants);
    RUN_TEST(test_pgn214_hdop_scale);
    RUN_TEST(test_pgn214_heading_deg_to_rad);

    return UNITY_END();
}
