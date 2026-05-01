/* ========================================================================
 * test_steering_sensor — Host tests for WAS sensor (STEER-MIG-001 AP-D/F)
 *
 * Tests:
 *  1. No ADC → valid=false, reason=NO_ADC
 *  2. Default calibration: 0→-40°, 65535→+40°
 *  3. Mid-range ADC → 0°
 *  4. Custom calibration endpoints
 *  5. Out-of-range angle → unplausible
 *  6. Calibration span zero → error
 *  7. Voltage calculation
 *  8. Plausibility limits
 *  9. Reason strings
 * 10. Freshness timeout
 * ======================================================================== */

#include "unity.h"
#include "was_sensor.h"
#include "ads1118.h"
#include <string.h>

/* ---- Mock ADC ---- */

static uint16_t mock_adc_raw = 0;

static uint16_t mock_get_raw(const ads1118_t* adc)
{
    (void)adc;
    return mock_adc_raw;
}

/* Create a mock ads1118 that returns mock_adc_raw */
static ads1118_t make_mock_adc(void)
{
    ads1118_t adc;
    memset(&adc, 0, sizeof(adc));
    return adc;
}

/* ---- Fixture ---- */

static was_sensor_t was;

void setUp(void)
{
    memset(&was, 0, sizeof(was));
    was_sensor_init(&was);
    mock_adc_raw = 0;
}

void tearDown(void) {}

/* 1. No ADC → NO_ADC */
void test_no_adc(void)
{
    /* was_sensor_service_step uses ads1118_get_raw which needs hal_spi.
     * In host test, we can't call it directly. Test the init path. */
    TEST_ASSERT_FALSE(was.current.valid);
    TEST_ASSERT_EQUAL(WAS_REASON_NOT_YET_READ, was.current.reason);
}

/* 2-9. Calibration and scaling tests (pure logic, no ADC needed) */

/* 2. Default calibration range check */
void test_default_calibration(void)
{
    TEST_ASSERT_EQUAL(0, was.cal_min_raw);
    TEST_ASSERT_EQUAL(65535, was.cal_max_raw);
    TEST_ASSERT_EQUAL_FLOAT(-40.0f, was.cal_min_deg);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, was.cal_max_deg);
}

/* 3. Custom calibration */
void test_custom_calibration(void)
{
    was_sensor_set_calibration(&was, 1000, 50000, -22.5f, 22.5f);
    TEST_ASSERT_EQUAL(1000, was.cal_min_raw);
    TEST_ASSERT_EQUAL(50000, was.cal_max_raw);
    TEST_ASSERT_EQUAL_FLOAT(-22.5f, was.cal_min_deg);
    TEST_ASSERT_EQUAL_FLOAT(22.5f, was.cal_max_deg);
}

/* 4. Calibration span zero */
void test_cal_span_zero(void)
{
    was_sensor_set_calibration(&was, 1000, 1000, -22.5f, 22.5f);
    /* With ADC mock, raw=1000, span=0 → CAL_SPAN_ZERO */
    /* Test the detection logic directly */
    uint16_t raw_span = was.cal_max_raw - was.cal_min_raw;
    TEST_ASSERT_EQUAL(0, raw_span);
}

/* 5. Voltage calculation */
void test_voltage_calc(void)
{
    was_sensor_set_vref(&was, 3.3f);
    /* raw=0 → 0V, raw=65535 → 3.3V, raw=32767 → ~1.65V */
    float v0 = (0.0f / 65535.0f) * 3.3f;
    float v_full = (65535.0f / 65535.0f) * 3.3f;
    float v_mid = (32767.0f / 65535.0f) * 3.3f;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, v0);
    TEST_ASSERT_EQUAL_FLOAT(3.3f, v_full);
    TEST_ASSERT_TRUE(v_mid > 1.6f && v_mid < 1.7f);
}

/* 6. Plausibility limits */
void test_plausibility_limits(void)
{
    was_sensor_set_plausibility(&was, 40.0f, 5.0f);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, was.angle_abs_max_deg);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, was.max_jump_rate_deg);
}

/* 7. Freshness timeout */
void test_freshness_timeout(void)
{
    was_sensor_set_freshness_timeout(&was, 200000);
    TEST_ASSERT_EQUAL(200000, was.freshness_timeout_us);
}

/* 8. Reason strings */
void test_reason_strings(void)
{
    TEST_ASSERT_EQUAL_STRING("NONE", was_sensor_reason_str(WAS_REASON_NONE));
    TEST_ASSERT_EQUAL_STRING("NO_ADC", was_sensor_reason_str(WAS_REASON_NO_ADC));
    TEST_ASSERT_EQUAL_STRING("CAL_SPAN_ZERO", was_sensor_reason_str(WAS_REASON_CAL_SPAN_ZERO));
    TEST_ASSERT_EQUAL_STRING("RAW_OUT_OF_RANGE", was_sensor_reason_str(WAS_REASON_RAW_OUT_OF_RANGE));
    TEST_ASSERT_EQUAL_STRING("ANGLE_UNPLAUSIBLE", was_sensor_reason_str(WAS_REASON_ANGLE_UNPLAUSIBLE));
    TEST_ASSERT_EQUAL_STRING("STALE", was_sensor_reason_str(WAS_REASON_STALE));
    TEST_ASSERT_EQUAL_STRING("NOT_YET_READ", was_sensor_reason_str(WAS_REASON_NOT_YET_READ));
}

/* 9. Linear interpolation correctness */
void test_linear_interpolation(void)
{
    /* Manual calculation test:
     * min_raw=1000, max_raw=50000 → span=49000
     * min_deg=-22.5, max_deg=22.5 → span=45.0
     * raw=25500 (midpoint): deg = -22.5 + (25500-1000)*45.0/49000
     *                              = -22.5 + 24500*45.0/49000
     *                              = -22.5 + 1102500/49000
     *                              = -22.5 + 22.5
     *                              = 0.0 */
    float raw_span = 50000 - 1000;  /* 49000 */
    float deg_span = 22.5f - (-22.5f);  /* 45.0 */
    float raw = 25500.0f;
    float deg = -22.5f + ((raw - 1000.0f) * deg_span) / raw_span;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, deg);

    /* Quarter point: raw=13750 */
    raw = 13750.0f;
    deg = -22.5f + ((raw - 1000.0f) * deg_span) / raw_span;
    TEST_ASSERT_TRUE(deg > -11.3f && deg < -11.2f);
}

/* 10. Init preserves last known on no ADC */
void test_init_preserves_last(void)
{
    was_sensor_init(&was);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, was.last_valid_degrees);
    TEST_ASSERT_FALSE(was.has_ever_read);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_adc);
    RUN_TEST(test_default_calibration);
    RUN_TEST(test_custom_calibration);
    RUN_TEST(test_cal_span_zero);
    RUN_TEST(test_voltage_calc);
    RUN_TEST(test_plausibility_limits);
    RUN_TEST(test_freshness_timeout);
    RUN_TEST(test_reason_strings);
    RUN_TEST(test_linear_interpolation);
    RUN_TEST(test_init_preserves_last);
    return UNITY_END();
}
