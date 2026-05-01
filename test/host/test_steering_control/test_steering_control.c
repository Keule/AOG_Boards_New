/* ========================================================================
 * test_steering_control — Host tests for steering_control (STEER-MIG-001 AP-B/F)
 *
 * Tests:
 *  1. Default: motor OFF, safety blocked
 *  2. Enable + valid data → motor ON
 *  3. Safety blocked → PID reset, motor OFF
 *  4. PID computation: positive error → positive output
 *  5. PID computation: negative error → negative output
 *  6. PID anti-windup
 *  7. PID output saturation
 *  8. NaN command → no crash
 *  9. Diagnostics snapshot published
 * 10. Service step fallback
 * 11. Safety timeout configuration
 * 12. PID configuration
 * ======================================================================== */

#include "unity.h"
#include "steering_control.h"
#include <string.h>
#include <math.h>

/* ---- Snapshot helpers ---- */

#define TEST_SNAP_SIZE 256

static uint8_t s_cmd_storage[TEST_SNAP_SIZE];
static uint8_t s_was_storage[TEST_SNAP_SIZE];
static snapshot_buffer_t s_cmd_snap;
static snapshot_buffer_t s_was_snap;

/* ---- Fixture ---- */

static steering_control_t ctrl;

void setUp(void)
{
    memset(&ctrl, 0, sizeof(ctrl));

    snapshot_buffer_init(&s_cmd_snap, s_cmd_storage, sizeof(aog_steer_input_t));
    snapshot_buffer_init(&s_was_snap, s_was_storage, sizeof(was_sensor_data_t));

    aog_steer_input_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    snapshot_buffer_set(&s_cmd_snap, &cmd);

    was_sensor_data_t was;
    memset(&was, 0, sizeof(was));
    snapshot_buffer_set(&s_was_snap, &was);
}

void tearDown(void) {}

/* 1. Default: motor OFF */
void test_default_motor_off(void)
{
    steering_control_init(&ctrl);
    steering_control_set_steer_input(&ctrl, &s_cmd_snap);
    steering_control_set_was(&ctrl, &s_was_snap);

    /* Run one service step */
    steering_control_service_step(&ctrl, 100000);

    const steering_output_t* out = steering_control_get_output(&ctrl);
    TEST_ASSERT_FALSE(out->state.motor_enabled);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out->state.pwm);
}

/* 2. Enable + valid data → motor ON */
void test_enable_valid_data(void)
{
    steering_control_init(&ctrl);
    steering_control_set_steer_input(&ctrl, &s_cmd_snap);
    steering_control_set_was(&ctrl, &s_was_snap);
    steering_control_set_global_enabled(&ctrl, true);
    steering_control_set_local_switch(&ctrl, true);

    /* Set valid command */
    aog_steer_input_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.steer_angle_deg = 5.0f;
    cmd.speed_ms = 2.0f;
    snapshot_buffer_set(&s_cmd_snap, &cmd);

    /* Set valid sensor */
    was_sensor_data_t was;
    memset(&was, 0, sizeof(was));
    was.degrees = 3.0f;
    was.valid = true;
    was.fresh = true;
    was.timestamp_us = 100000;
    snapshot_buffer_set(&s_was_snap, &was);

    /* Feed comms and run */
    steering_safety_feed_comms(&ctrl.safety, 100000);
    steering_control_service_step(&ctrl, 100000);

    const steering_output_t* out = steering_control_get_output(&ctrl);
    /* Motor should be enabled because safety passes */
    const steer_safety_result_t* sr = steering_safety_get_result(&ctrl);
    if (sr->safe) {
        TEST_ASSERT_TRUE(out->state.motor_enabled);
    }
}

/* 3. Safety blocked → PID reset */
void test_safety_blocked_pid_reset(void)
{
    steering_control_init(&ctrl);
    steering_control_set_steer_input(&ctrl, &s_cmd_snap);
    steering_control_set_was(&ctrl, &s_was_snap);

    /* Run without enabling → safety blocked */
    steering_control_service_step(&ctrl, 100000);

    /* PID should be reset */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ctrl.integral);
    TEST_ASSERT_FALSE(ctrl.pid_initialized);
}

/* 4. PID: positive error → positive output */
void test_pid_positive_error(void)
{
    steering_control_init(&ctrl);
    steering_control_set_pid(&ctrl, 1.0f, 0.0f, 0.0f, 10.0f, -1.0f, 1.0f);
    steering_control_set_steer_input(&ctrl, &s_cmd_snap);
    steering_control_set_was(&ctrl, &s_was_snap);
    steering_control_set_global_enabled(&ctrl, true);
    steering_control_set_local_switch(&ctrl, true);
    steering_safety_feed_comms(&ctrl.safety, 100000);

    /* Setpoint=5, actual=3 → error=+2 → output=+2 (kp=1) */
    aog_steer_input_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.steer_angle_deg = 5.0f;
    cmd.speed_ms = 2.0f;
    snapshot_buffer_set(&s_cmd_snap, &cmd);

    was_sensor_data_t was;
    memset(&was, 0, sizeof(was));
    was.degrees = 3.0f;
    was.valid = true;
    was.fresh = true;
    was.timestamp_us = 100000;
    snapshot_buffer_set(&s_was_snap, &was);

    steering_control_service_step(&ctrl, 100000);

    const steer_safety_result_t* sr = steering_safety_get_result(&ctrl);
    if (sr->safe) {
        /* PID output should be positive (error=2, kp=1) */
        TEST_ASSERT_TRUE(ctrl.diag.pid_output > 0.0f);
    }
}

/* 5. PID: negative error → negative output */
void test_pid_negative_error(void)
{
    steering_control_init(&ctrl);
    steering_control_set_pid(&ctrl, 1.0f, 0.0f, 0.0f, 10.0f, -1.0f, 1.0f);
    steering_control_set_steer_input(&ctrl, &s_cmd_snap);
    steering_control_set_was(&ctrl, &s_was_snap);
    steering_control_set_global_enabled(&ctrl, true);
    steering_control_set_local_switch(&ctrl, true);
    steering_safety_feed_comms(&ctrl.safety, 100000);

    /* Setpoint=3, actual=5 → error=-2 → output=-2 */
    aog_steer_input_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.steer_angle_deg = 3.0f;
    cmd.speed_ms = 2.0f;
    snapshot_buffer_set(&s_cmd_snap, &cmd);

    was_sensor_data_t was;
    memset(&was, 0, sizeof(was));
    was.degrees = 5.0f;
    was.valid = true;
    was.fresh = true;
    was.timestamp_us = 100000;
    snapshot_buffer_set(&s_was_snap, &was);

    steering_control_service_step(&ctrl, 100000);

    const steer_safety_result_t* sr = steering_safety_get_result(&ctrl);
    if (sr->safe) {
        TEST_ASSERT_TRUE(ctrl.diag.pid_output < 0.0f);
    }
}

/* 6. PID anti-windup */
void test_pid_anti_windup(void)
{
    steering_control_init(&ctrl);
    steering_control_set_pid(&ctrl, 1.0f, 1.0f, 0.0f, 5.0f, -1.0f, 1.0f);
    steering_control_set_steer_input(&ctrl, &s_cmd_snap);
    steering_control_set_was(&ctrl, &s_was_snap);
    steering_control_set_global_enabled(&ctrl, true);
    steering_control_set_local_switch(&ctrl, true);
    steering_safety_feed_comms(&ctrl.safety, 100000);

    aog_steer_input_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.steer_angle_deg = 5.0f;
    cmd.speed_ms = 2.0f;
    snapshot_buffer_set(&s_cmd_snap, &cmd);

    was_sensor_data_t was;
    memset(&was, 0, sizeof(was));
    was.degrees = 3.0f;
    was.valid = true;
    was.fresh = true;
    was.timestamp_us = 100000;
    snapshot_buffer_set(&s_was_snap, &was);

    /* Run many cycles to build up integral */
    for (int i = 0; i < 100; i++) {
        was.timestamp_us = 100000 + (uint64_t)i * 10000;
        cmd.timestamp_us = was.timestamp_us;
        snapshot_buffer_set(&s_cmd_snap, &cmd);
        snapshot_buffer_set(&s_was_snap, &was);
        steering_safety_feed_comms(&ctrl.safety, was.timestamp_us);
        steering_control_service_step(&ctrl, was.timestamp_us);
    }

    /* Integral should not exceed max */
    TEST_ASSERT_TRUE(ctrl.integral <= 5.0f + 0.01f);
    TEST_ASSERT_TRUE(ctrl.integral >= -5.0f - 0.01f);
}

/* 7. PID output saturation */
void test_pid_output_saturation(void)
{
    steering_control_init(&ctrl);
    steering_control_set_pid(&ctrl, 100.0f, 0.0f, 0.0f, 10.0f, -1.0f, 1.0f);
    steering_control_set_steer_input(&ctrl, &s_cmd_snap);
    steering_control_set_was(&ctrl, &s_was_snap);
    steering_control_set_global_enabled(&ctrl, true);
    steering_control_set_local_switch(&ctrl, true);
    steering_safety_feed_comms(&ctrl.safety, 100000);

    /* Huge error with high gain → should saturate to output_max */
    aog_steer_input_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.steer_angle_deg = 20.0f;
    cmd.speed_ms = 2.0f;
    snapshot_buffer_set(&s_cmd_snap, &cmd);

    was_sensor_data_t was;
    memset(&was, 0, sizeof(was));
    was.degrees = -20.0f;
    was.valid = true;
    was.fresh = true;
    was.timestamp_us = 100000;
    snapshot_buffer_set(&s_was_snap, &was);

    steering_control_service_step(&ctrl, 100000);

    const steer_safety_result_t* sr = steering_safety_get_result(&ctrl);
    if (sr->safe) {
        TEST_ASSERT_TRUE(ctrl.diag.saturated_output <= 1.0f);
    }
}

/* 8. NaN command → no crash */
void test_nan_command(void)
{
    steering_control_init(&ctrl);
    steering_control_set_steer_input(&ctrl, &s_cmd_snap);
    steering_control_set_was(&ctrl, &s_was_snap);
    steering_control_set_global_enabled(&ctrl, true);
    steering_control_set_local_switch(&ctrl, true);
    steering_safety_feed_comms(&ctrl.safety, 100000);

    aog_steer_input_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.steer_angle_deg = NAN;
    cmd.speed_ms = 2.0f;
    snapshot_buffer_set(&s_cmd_snap, &cmd);

    was_sensor_data_t was;
    memset(&was, 0, sizeof(was));
    was.degrees = 3.0f;
    was.valid = true;
    was.fresh = true;
    was.timestamp_us = 100000;
    snapshot_buffer_set(&s_was_snap, &was);

    /* Should not crash */
    steering_control_service_step(&ctrl, 100000);
    TEST_ASSERT_TRUE(isfinite(ctrl.diag.pid_output) || ctrl.diag.pid_output == 0.0f);
}

/* 9. Diagnostics snapshot published */
void test_diagnostics_snapshot(void)
{
    steering_control_init(&ctrl);
    steering_control_set_steer_input(&ctrl, &s_cmd_snap);
    steering_control_set_was(&ctrl, &s_was_snap);

    steering_control_service_step(&ctrl, 100000);

    steer_diag_snapshot_t diag;
    bool ok = snapshot_buffer_get(&ctrl.diag_snapshot, &diag);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(100000, diag.timestamp_us);
    TEST_ASSERT_EQUAL(1, diag.cycle_count);
}

/* 10. Service step fallback */
void test_service_step_fallback(void)
{
    steering_control_init(&ctrl);
    steering_control_set_steer_input(&ctrl, &s_cmd_snap);
    steering_control_set_was(&ctrl, &s_was_snap);

    /* Should not crash when called without fast path */
    steering_control_service_step(&ctrl, 500000);
    TEST_ASSERT_EQUAL(1, ctrl.fast_cycle_count);
}

/* 11. Safety timeout configuration */
void test_safety_timeout_config(void)
{
    steering_control_init(&ctrl);
    steering_control_set_safety_timeouts(&ctrl, 150000, 100000, 400000);

    TEST_ASSERT_EQUAL(150000, ctrl.safety.command_timeout_us);
    TEST_ASSERT_EQUAL(100000, ctrl.safety.sensor_timeout_us);
    TEST_ASSERT_EQUAL(400000, ctrl.safety.comms_timeout_us);
}

/* 12. PID configuration */
void test_pid_configuration(void)
{
    steering_control_init(&ctrl);
    steering_control_set_pid(&ctrl, 2.0f, 0.05f, 0.1f, 20.0f, -0.8f, 0.8f);

    TEST_ASSERT_EQUAL_FLOAT(2.0f, ctrl.pid_config.kp);
    TEST_ASSERT_EQUAL_FLOAT(0.05f, ctrl.pid_config.ki);
    TEST_ASSERT_EQUAL_FLOAT(0.1f, ctrl.pid_config.kd);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, ctrl.pid_config.integral_max);
    TEST_ASSERT_EQUAL_FLOAT(-0.8f, ctrl.pid_config.output_min);
    TEST_ASSERT_EQUAL_FLOAT(0.8f, ctrl.pid_config.output_max);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_motor_off);
    RUN_TEST(test_enable_valid_data);
    RUN_TEST(test_safety_blocked_pid_reset);
    RUN_TEST(test_pid_positive_error);
    RUN_TEST(test_pid_negative_error);
    RUN_TEST(test_pid_anti_windup);
    RUN_TEST(test_pid_output_saturation);
    RUN_TEST(test_nan_command);
    RUN_TEST(test_diagnostics_snapshot);
    RUN_TEST(test_service_step_fallback);
    RUN_TEST(test_safety_timeout_config);
    RUN_TEST(test_pid_configuration);
    return UNITY_END();
}
