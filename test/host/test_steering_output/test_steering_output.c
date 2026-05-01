/* ========================================================================
 * test_steering_output — Host tests for steering_output (STEER-MIG-001 AP-E/F)
 *
 * Tests:
 *  1. Default: motor OFF, PWM=0, safety blocked
 *  2. Safety false → motor OFF, PWM=0
 *  3. Safety true + zero command → motor ON, PWM=0
 *  4. Safety true + positive command → right direction
 *  5. Safety true + negative command → left direction
 *  6. Deadzone: small commands produce zero PWM
 *  7. Saturation: commands > 1.0 clamped
 *  8. NaN guard: NaN input → PWM=0
 *  9. Inf guard: Inf input → PWM=0
 * 10. force_off overrides everything
 * 11. Mock HAL inspection
 * 12. Deadzone boundary
 * ======================================================================== */

#include "unity.h"
#include "steering_output.h"
#include <math.h>
#include <string.h>

static steering_output_t out;
static steering_output_hal_t mock_hal;

void setUp(void)
{
    steering_output_mock_hal_init(&mock_hal);
    steering_output_init(&out, &mock_hal);
}

void tearDown(void) {}

/* 1. Default: motor OFF */
void test_default_motor_off(void)
{
    TEST_ASSERT_FALSE(out.state.motor_enabled);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.state.pwm);
    TEST_ASSERT_TRUE(out.state.safety_blocked);
}

/* 2. Safety false → motor OFF */
void test_safety_false(void)
{
    steering_output_update(&out, 0.5f, false, 100000);
    TEST_ASSERT_FALSE(out.state.motor_enabled);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.state.pwm);
    TEST_ASSERT_TRUE(out.state.safety_blocked);
    TEST_ASSERT_EQUAL(1, out.safety_block_count);
}

/* 3. Safety true + zero command → motor ON, PWM=0 */
void test_zero_command(void)
{
    steering_output_update(&out, 0.0f, true, 100000);
    TEST_ASSERT_TRUE(out.state.motor_enabled);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.state.pwm);
    TEST_ASSERT_FALSE(out.state.safety_blocked);
}

/* 4. Positive command → right direction */
void test_positive_command(void)
{
    steering_output_update(&out, 0.5f, true, 100000);
    TEST_ASSERT_TRUE(out.state.motor_enabled);
    TEST_ASSERT_TRUE(out.state.direction);  /* right */
    TEST_ASSERT_TRUE(out.state.pwm > 0.0f);
}

/* 5. Negative command → left direction */
void test_negative_command(void)
{
    steering_output_update(&out, -0.5f, true, 100000);
    TEST_ASSERT_TRUE(out.state.motor_enabled);
    TEST_ASSERT_FALSE(out.state.direction);  /* left */
    TEST_ASSERT_TRUE(out.state.pwm < 0.0f);
}

/* 6. Deadzone */
void test_deadzone(void)
{
    steering_output_update(&out, 0.03f, true, 100000);  /* within 5% deadzone */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.state.pwm);

    steering_output_update(&out, -0.03f, true, 100000);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.state.pwm);

    steering_output_update(&out, 0.06f, true, 100000);  /* above deadzone */
    TEST_ASSERT_TRUE(out.state.pwm > 0.0f);
}

/* 7. Saturation */
void test_saturation(void)
{
    steering_output_update(&out, 2.0f, true, 100000);  /* > 1.0 */
    TEST_ASSERT_TRUE(out.state.pwm <= 1.0f);
    TEST_ASSERT_TRUE(out.state.pwm > 0.0f);

    steering_output_update(&out, -2.0f, true, 100000);
    TEST_ASSERT_TRUE(out.state.pwm >= -1.0f);
    TEST_ASSERT_TRUE(out.state.pwm < 0.0f);
}

/* 8. NaN guard */
void test_nan_guard(void)
{
    steering_output_update(&out, NAN, true, 100000);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.state.pwm);
}

/* 9. Inf guard */
void test_inf_guard(void)
{
    steering_output_update(&out, INFINITY, true, 100000);
    TEST_ASSERT_TRUE(out.state.pwm >= 0.0f && out.state.pwm <= 1.0f);

    steering_output_update(&out, -INFINITY, true, 100000);
    TEST_ASSERT_TRUE(out.state.pwm >= -1.0f && out.state.pwm <= 0.0f);
}

/* 10. force_off */
void test_force_off(void)
{
    steering_output_update(&out, 0.5f, true, 100000);
    TEST_ASSERT_TRUE(out.state.motor_enabled);

    steering_output_force_off(&out);
    TEST_ASSERT_FALSE(out.state.motor_enabled);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.state.pwm);
    TEST_ASSERT_TRUE(out.state.safety_blocked);
}

/* 11. Mock HAL inspection */
void test_mock_hal_inspection(void)
{
    /* Track mock writes via a custom HAL */
    static float last_pwm_val = 99.0f;
    static bool last_en_val = false;
    static bool last_dir_val = false;
    static bool emergency_called = false;

    steering_output_hal_t custom_hal;
    memset(&custom_hal, 0, sizeof(custom_hal));

    custom_hal.set_enable = [](bool v) { last_en_val = v; };
    custom_hal.set_direction = [](bool v) { last_dir_val = v; };
    custom_hal.set_pwm = [](float v) { last_pwm_val = v; };
    custom_hal.emergency_stop = []() { emergency_called = true; };

    steering_output_t custom_out;
    steering_output_init(&custom_out, &custom_hal);

    /* Reset tracking */
    last_pwm_val = 99.0f;
    last_en_val = true;
    emergency_called = false;

    /* Safety false → should NOT call set_enable(true) or set_pwm */
    steering_output_update(&custom_out, 0.5f, false, 100000);
    TEST_ASSERT_FALSE(last_en_val);  /* should be false (motor off) */
}

/* 12. Deadzone boundary */
void test_deadzone_boundary(void)
{
    /* Exact deadzone boundary (0.05) should produce zero */
    steering_output_update(&out, 0.05f, true, 100000);
    /* 0.05 == deadzone → zero */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.state.pwm);

    /* Just above */
    steering_output_update(&out, 0.0501f, true, 100000);
    TEST_ASSERT_TRUE(out.state.pwm > 0.0f);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_motor_off);
    RUN_TEST(test_safety_false);
    RUN_TEST(test_zero_command);
    RUN_TEST(test_positive_command);
    RUN_TEST(test_negative_command);
    RUN_TEST(test_deadzone);
    RUN_TEST(test_saturation);
    RUN_TEST(test_nan_guard);
    RUN_TEST(test_inf_guard);
    RUN_TEST(test_force_off);
    RUN_TEST(test_mock_hal_inspection);
    RUN_TEST(test_deadzone_boundary);
    return UNITY_END();
}
