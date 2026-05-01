/* ========================================================================
 * test_steering_safety — Host tests for steering_safety (STEER-MIG-001 AP-C/F)
 *
 * Tests all 10 mandatory safety conditions + default states + statistics.
 *
 * Test cases:
 *  1. Default after init: motor OFF (not enabled)
 *  2. Global disabled → Motor OFF
 *  3. Local switch OFF → Motor OFF
 *  4. Internal fault → Motor OFF
 *  5. Communication lost → Motor OFF
 *  6. No valid command → Motor OFF
 *  7. Command stale → Motor OFF
 *  8. Sensor invalid → Motor OFF
 *  9. Sensor stale → Motor OFF
 * 10. Sensor unplausible → Motor OFF
 * 11. Setpoint OOR → clamped (not motor off)
 * 12. All conditions pass → SAFE
 * 13. Clear fault restores operation
 * 14. Timeout configuration
 * 15. Statistics tracking
 * 16. Reason string mapping
 * 17. NULL safety gate → INTERNAL_FAULT
 * 18. Command timeout boundary (exactly at timeout = fresh)
 * 19. Setpoint clamping at exact boundaries
 * ======================================================================== */

#include "unity.h"
#include "steering_safety.h"
#include <string.h>

/* ---- Helpers ---- */

static steer_command_input_t make_valid_cmd(float angle, uint64_t ts)
{
    steer_command_input_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.setpoint_deg  = angle;
    cmd.speed_ms      = 2.0f;
    cmd.status        = 1;
    cmd.valid         = true;
    cmd.sensors_valid = true;
    cmd.timestamp_us  = ts;
    return cmd;
}

static steer_sensor_input_t make_valid_sensor(float angle, uint64_t ts)
{
    steer_sensor_input_t sensor;
    memset(&sensor, 0, sizeof(sensor));
    sensor.angle_deg    = angle;
    sensor.valid        = true;
    sensor.fresh        = true;
    sensor.timestamp_us = ts;
    return sensor;
}

/* ---- Fixture ---- */

static steering_safety_t sg;

void setUp(void)
{
    steering_safety_init(&sg);
}

void tearDown(void) {}

/* ==================================================================
 * Tests
 * ================================================================== */

/* 1. Default after init: motor OFF */
void test_default_motor_off(void)
{
    steer_safety_result_t r = steering_safety_evaluate(&sg, NULL, NULL, 100000);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_GLOBAL_DISABLED, r.reason);
}

/* 2. Global disabled → Motor OFF */
void test_global_disabled(void)
{
    steering_safety_set_global_enabled(&sg, false);
    steer_safety_result_t r = steering_safety_evaluate(&sg, NULL, NULL, 100000);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_GLOBAL_DISABLED, r.reason);
}

/* 3. Local switch OFF → Motor OFF */
void test_local_switch_off(void)
{
    steering_safety_set_global_enabled(&sg, true);
    /* local_switch defaults to false */
    steer_safety_result_t r = steering_safety_evaluate(&sg, NULL, NULL, 100000);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_LOCAL_SWITCH_OFF, r.reason);
}

/* 4. Internal fault → Motor OFF */
void test_internal_fault(void)
{
    steering_safety_set_global_enabled(&sg, true);
    steering_safety_set_local_switch(&sg, true);
    steering_safety_set_internal_fault(&sg, true);

    steer_safety_result_t r = steering_safety_evaluate(&sg, NULL, NULL, 100000);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_INTERNAL_FAULT, r.reason);
}

/* 5. Communication lost → Motor OFF */
void test_comms_lost(void)
{
    steering_safety_set_global_enabled(&sg, true);
    steering_safety_set_local_switch(&sg, true);
    /* Never called feed_comms → comms lost */
    steer_safety_result_t r = steering_safety_evaluate(&sg, NULL, NULL, 100000);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_COMMS_LOST, r.reason);
}

/* 6. No valid command → Motor OFF */
void test_no_valid_command(void)
{
    steering_safety_set_global_enabled(&sg, true);
    steering_safety_set_local_switch(&sg, true);
    steering_safety_feed_comms(&sg, 100000);

    /* NULL command → COMMAND_INVALID */
    steer_safety_result_t r = steering_safety_evaluate(&sg, NULL, NULL, 100000);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_COMMAND_INVALID, r.reason);

    /* Invalid command */
    steer_command_input_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.valid = false;
    r = steering_safety_evaluate(&sg, &cmd, NULL, 100000);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_COMMAND_INVALID, r.reason);
}

/* 7. Command stale → Motor OFF */
void test_command_stale(void)
{
    steering_safety_set_global_enabled(&sg, true);
    steering_safety_set_local_switch(&sg, true);
    steering_safety_feed_comms(&sg, 100000);

    steer_command_input_t cmd = make_valid_cmd(5.0f, 100000);
    steer_sensor_input_t sensor = make_valid_sensor(3.0f, 100000);

    /* Command at 100000, now at 500000 → stale (timeout=200ms=200000us) */
    steer_safety_result_t r = steering_safety_evaluate(&sg, &cmd, &sensor, 500000);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_COMMAND_STALE, r.reason);
}

/* 8. Sensor invalid → Motor OFF */
void test_sensor_invalid(void)
{
    steering_safety_set_global_enabled(&sg, true);
    steering_safety_set_local_switch(&sg, true);
    steering_safety_feed_comms(&sg, 100000);

    steer_command_input_t cmd = make_valid_cmd(5.0f, 100000);
    steer_sensor_input_t sensor;
    memset(&sensor, 0, sizeof(sensor));
    sensor.valid = false;

    steer_safety_result_t r = steering_safety_evaluate(&sg, &cmd, &sensor, 100000);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_SENSOR_INVALID, r.reason);

    /* NULL sensor */
    r = steering_safety_evaluate(&sg, &cmd, NULL, 100000);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_SENSOR_INVALID, r.reason);
}

/* 9. Sensor stale → Motor OFF */
void test_sensor_stale(void)
{
    steering_safety_set_global_enabled(&sg, true);
    steering_safety_set_local_switch(&sg, true);
    steering_safety_feed_comms(&sg, 100000);

    steer_command_input_t cmd = make_valid_cmd(5.0f, 100000);
    steer_sensor_input_t sensor;
    memset(&sensor, 0, sizeof(sensor));
    sensor.angle_deg    = 3.0f;
    sensor.valid        = true;
    sensor.fresh        = false;  /* explicitly stale */
    sensor.timestamp_us = 100000;

    steer_safety_result_t r = steering_safety_evaluate(&sg, &cmd, &sensor, 100000);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_SENSOR_STALE, r.reason);
}

/* 10. Sensor unplausible → Motor OFF */
void test_sensor_unplausible(void)
{
    steering_safety_set_global_enabled(&sg, true);
    steering_safety_set_local_switch(&sg, true);
    steering_safety_feed_comms(&sg, 100000);

    steer_command_input_t cmd = make_valid_cmd(5.0f, 100000);
    steer_sensor_input_t sensor = make_valid_sensor(50.0f, 100000);  /* > 40° abs max */

    steer_safety_result_t r = steering_safety_evaluate(&sg, &cmd, &sensor, 100000);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_SENSOR_UNPLAUSIBLE, r.reason);

    /* Negative unplausible */
    sensor.angle_deg = -50.0f;
    r = steering_safety_evaluate(&sg, &cmd, &sensor, 100000);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_SENSOR_UNPLAUSIBLE, r.reason);
}

/* 11. Setpoint OOR → clamped (not motor off) */
void test_setpoint_oor_clamped(void)
{
    steering_safety_set_global_enabled(&sg, true);
    steering_safety_set_local_switch(&sg, true);
    steering_safety_feed_comms(&sg, 100000);

    steer_command_input_t cmd = make_valid_cmd(30.0f, 100000);  /* > 22.5° */
    steer_sensor_input_t sensor = make_valid_sensor(5.0f, 100000);

    steer_safety_result_t r = steering_safety_evaluate(&sg, &cmd, &sensor, 100000);
    TEST_ASSERT_TRUE(r.safe);
    TEST_ASSERT_TRUE(r.setpoint_clamped);
    TEST_ASSERT_EQUAL_FLOAT(22.5f, r.clamped_setpoint_deg);

    /* Negative OOR */
    cmd.setpoint_deg = -30.0f;
    r = steering_safety_evaluate(&sg, &cmd, &sensor, 100000);
    TEST_ASSERT_TRUE(r.safe);
    TEST_ASSERT_TRUE(r.setpoint_clamped);
    TEST_ASSERT_EQUAL_FLOAT(-22.5f, r.clamped_setpoint_deg);
}

/* 12. All conditions pass → SAFE */
void test_all_safe(void)
{
    steering_safety_set_global_enabled(&sg, true);
    steering_safety_set_local_switch(&sg, true);
    steering_safety_feed_comms(&sg, 100000);

    steer_command_input_t cmd = make_valid_cmd(5.0f, 100000);
    steer_sensor_input_t sensor = make_valid_sensor(3.0f, 100000);

    steer_safety_result_t r = steering_safety_evaluate(&sg, &cmd, &sensor, 100000);
    TEST_ASSERT_TRUE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_OK, r.reason);
    TEST_ASSERT_FALSE(r.setpoint_clamped);
}

/* 13. Clear fault restores operation */
void test_clear_fault(void)
{
    steering_safety_set_global_enabled(&sg, true);
    steering_safety_set_local_switch(&sg, true);
    steering_safety_feed_comms(&sg, 100000);
    steering_safety_set_internal_fault(&sg, true);

    steer_command_input_t cmd = make_valid_cmd(5.0f, 100000);
    steer_sensor_input_t sensor = make_valid_sensor(3.0f, 100000);

    steer_safety_result_t r = steering_safety_evaluate(&sg, &cmd, &sensor, 100000);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_INTERNAL_FAULT, r.reason);

    /* Clear fault */
    steering_safety_clear_fault(&sg);
    r = steering_safety_evaluate(&sg, &cmd, &sensor, 100000);
    TEST_ASSERT_TRUE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_OK, r.reason);
}

/* 14. Timeout configuration */
void test_timeout_config(void)
{
    steering_safety_set_timeouts(&sg, 100000, 50000, 300000);
    TEST_ASSERT_EQUAL(100000, sg.command_timeout_us);
    TEST_ASSERT_EQUAL(50000, sg.sensor_timeout_us);
    TEST_ASSERT_EQUAL(300000, sg.comms_timeout_us);
}

/* 15. Statistics tracking */
void test_statistics(void)
{
    steering_safety_set_global_enabled(&sg, true);

    /* Evaluate once with global disabled → should increment unsafe_count */
    steering_safety_evaluate(&sg, NULL, NULL, 100000);
    TEST_ASSERT_EQUAL(1, sg.unsafe_count);
    TEST_ASSERT_EQUAL(1, sg.reason_counts[STEER_SAFETY_GLOBAL_DISABLED]);
    TEST_ASSERT_EQUAL(1, sg.eval_count);

    /* Enable and evaluate safe */
    steering_safety_set_local_switch(&sg, true);
    steering_safety_feed_comms(&sg, 100000);
    steer_command_input_t cmd = make_valid_cmd(5.0f, 100000);
    steer_sensor_input_t sensor = make_valid_sensor(3.0f, 100000);
    steering_safety_evaluate(&sg, &cmd, &sensor, 100000);
    TEST_ASSERT_TRUE(sg.result.safe);
    TEST_ASSERT_EQUAL(1, sg.unsafe_count);  /* unchanged */
    TEST_ASSERT_EQUAL(2, sg.eval_count);
}

/* 16. Reason string mapping */
void test_reason_strings(void)
{
    TEST_ASSERT_EQUAL_STRING("OK", steering_safety_reason_str(STEER_SAFETY_OK));
    TEST_ASSERT_EQUAL_STRING("GLOBAL_DISABLED", steering_safety_reason_str(STEER_SAFETY_GLOBAL_DISABLED));
    TEST_ASSERT_EQUAL_STRING("LOCAL_SWITCH_OFF", steering_safety_reason_str(STEER_SAFETY_LOCAL_SWITCH_OFF));
    TEST_ASSERT_EQUAL_STRING("COMMAND_STALE", steering_safety_reason_str(STEER_SAFETY_COMMAND_STALE));
    TEST_ASSERT_EQUAL_STRING("SENSOR_STALE", steering_safety_reason_str(STEER_SAFETY_SENSOR_STALE));
    TEST_ASSERT_EQUAL_STRING("SENSOR_UNPLAUSIBLE", steering_safety_reason_str(STEER_SAFETY_SENSOR_UNPLAUSIBLE));
    TEST_ASSERT_EQUAL_STRING("COMMS_LOST", steering_safety_reason_str(STEER_SAFETY_COMMS_LOST));
    TEST_ASSERT_EQUAL_STRING("INTERNAL_FAULT", steering_safety_reason_str(STEER_SAFETY_INTERNAL_FAULT));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", steering_safety_reason_str((steer_safety_reason_t)99));
}

/* 17. NULL safety gate → INTERNAL_FAULT */
void test_null_safety_gate(void)
{
    steer_command_input_t cmd = make_valid_cmd(5.0f, 100000);
    steer_sensor_input_t sensor = make_valid_sensor(3.0f, 100000);

    steer_safety_result_t r = steering_safety_evaluate(NULL, &cmd, &sensor, 100000);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_INTERNAL_FAULT, r.reason);
}

/* 18. Command timeout boundary */
void test_command_timeout_boundary(void)
{
    steering_safety_set_global_enabled(&sg, true);
    steering_safety_set_local_switch(&sg, true);
    steering_safety_feed_comms(&sg, 100000);

    steer_command_input_t cmd = make_valid_cmd(5.0f, 100000);
    steer_sensor_input_t sensor = make_valid_sensor(3.0f, 100000);

    /* Exactly at timeout → still fresh (<=) */
    steer_safety_result_t r = steering_safety_evaluate(&sg, &cmd, &sensor,
                                                       100000 + sg.command_timeout_us);
    TEST_ASSERT_TRUE(r.safe);

    /* 1us past timeout → stale */
    r = steering_safety_evaluate(&sg, &cmd, &sensor,
                                  100000 + sg.command_timeout_us + 1);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL(STEER_SAFETY_COMMAND_STALE, r.reason);
}

/* 19. Setpoint clamping at exact boundaries */
void test_setpoint_exact_boundary(void)
{
    steering_safety_set_global_enabled(&sg, true);
    steering_safety_set_local_switch(&sg, true);
    steering_safety_feed_comms(&sg, 100000);

    steer_command_input_t cmd = make_valid_cmd(22.5f, 100000);  /* exact max */
    steer_sensor_input_t sensor = make_valid_sensor(3.0f, 100000);

    steer_safety_result_t r = steering_safety_evaluate(&sg, &cmd, &sensor, 100000);
    TEST_ASSERT_TRUE(r.safe);
    TEST_ASSERT_FALSE(r.setpoint_clamped);  /* exactly at boundary → no clamp */

    cmd.setpoint_deg = 22.51f;  /* just over */
    r = steering_safety_evaluate(&sg, &cmd, &sensor, 100000);
    TEST_ASSERT_TRUE(r.safe);
    TEST_ASSERT_TRUE(r.setpoint_clamped);
    TEST_ASSERT_EQUAL_FLOAT(22.5f, r.clamped_setpoint_deg);
}

/* ---- Main ---- */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_motor_off);
    RUN_TEST(test_global_disabled);
    RUN_TEST(test_local_switch_off);
    RUN_TEST(test_internal_fault);
    RUN_TEST(test_comms_lost);
    RUN_TEST(test_no_valid_command);
    RUN_TEST(test_command_stale);
    RUN_TEST(test_sensor_invalid);
    RUN_TEST(test_sensor_stale);
    RUN_TEST(test_sensor_unplausible);
    RUN_TEST(test_setpoint_oor_clamped);
    RUN_TEST(test_all_safe);
    RUN_TEST(test_clear_fault);
    RUN_TEST(test_timeout_config);
    RUN_TEST(test_statistics);
    RUN_TEST(test_reason_strings);
    RUN_TEST(test_null_safety_gate);
    RUN_TEST(test_command_timeout_boundary);
    RUN_TEST(test_setpoint_exact_boundary);
    return UNITY_END();
}
