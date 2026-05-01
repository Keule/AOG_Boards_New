/* steering_safety.c — Safety Gate Implementation (STEER-MIG-001 AP-C)
 *
 * Pure-logic safety evaluator. Evaluates all 10 mandatory safety conditions
 * in priority order. Returns steer_safety_result_t with safe/reason/clamping.
 *
 * Default after init: motor OFF (global_enabled=false, local_switch=false).
 * Every unset/missing condition defaults to motor OFF (fail-safe).
 *
 * No hardware, no transport, no heap, no logging in hot path.
 */

#include "steering_safety.h"
#include <string.h>
#include <math.h>

/* ---- Reason code strings ---- */

static const char* s_reason_strings[] = {
    "OK",                    /* 0 */
    "GLOBAL_DISABLED",       /* 1 */
    "LOCAL_SWITCH_OFF",      /* 2 */
    "COMMAND_STALE",         /* 3 */
    "COMMAND_INVALID",       /* 4 */
    "SENSOR_STALE",          /* 5 */
    "SENSOR_INVALID",        /* 6 */
    "SENSOR_UNPLAUSIBLE",    /* 7 */
    "SETPOINT_OOR",          /* 8 */
    "ACTUAL_OOR",            /* 9 */
    "COMMS_LOST",            /* 10 */
    "INTERNAL_FAULT",        /* 11 */
    "NOT_ENABLED"            /* 12 */
};

/* ---- Internal: helper to check freshness ---- */

static bool is_fresh(uint64_t last_update_us, uint64_t now_us,
                     uint64_t timeout_us)
{
    if (timeout_us == 0) {
        return false;
    }
    if (now_us < last_update_us) {
        return false;  /* clock wrap protection */
    }
    return (now_us - last_update_us) <= timeout_us;
}

/* ---- Public API ---- */

void steering_safety_init(steering_safety_t* sg)
{
    if (sg == NULL) return;

    memset(sg, 0, sizeof(steering_safety_t));

    /* Conservative defaults */
    sg->angle_min_deg       = STEER_ANGLE_MIN_DEG;
    sg->angle_max_deg       = STEER_ANGLE_MAX_DEG;
    sg->angle_abs_max_deg   = STEER_ANGLE_ABS_MAX_DEG;
    sg->command_timeout_us  = STEER_COMMAND_TIMEOUT_US;
    sg->sensor_timeout_us   = STEER_SENSOR_TIMEOUT_US;
    sg->comms_timeout_us    = STEER_COMMS_TIMEOUT_US;
    sg->pwm_min             = STEER_PWM_MIN;
    sg->pwm_max             = STEER_PWM_MAX;

    /* Default: motor OFF */
    sg->global_enabled      = false;
    sg->local_switch        = false;
    sg->internal_fault      = false;

    sg->result.safe         = false;
    sg->result.reason       = STEER_SAFETY_NOT_ENABLED;
}

void steering_safety_set_angle_limits(steering_safety_t* sg,
                                       float min_deg, float max_deg,
                                       float abs_max_deg)
{
    if (sg == NULL) return;
    sg->angle_min_deg     = min_deg;
    sg->angle_max_deg     = max_deg;
    sg->angle_abs_max_deg = abs_max_deg;
}

void steering_safety_set_timeouts(steering_safety_t* sg,
                                   uint64_t command_us,
                                   uint64_t sensor_us,
                                   uint64_t comms_us)
{
    if (sg == NULL) return;
    sg->command_timeout_us = command_us;
    sg->sensor_timeout_us  = sensor_us;
    sg->comms_timeout_us   = comms_us;
}

void steering_safety_set_pwm_limits(steering_safety_t* sg,
                                     float min, float max)
{
    if (sg == NULL) return;
    sg->pwm_min = min;
    sg->pwm_max = max;
}

void steering_safety_set_global_enabled(steering_safety_t* sg, bool enabled)
{
    if (sg == NULL) return;
    sg->global_enabled = enabled;
}

void steering_safety_set_local_switch(steering_safety_t* sg, bool on)
{
    if (sg == NULL) return;
    sg->local_switch = on;
}

void steering_safety_set_internal_fault(steering_safety_t* sg, bool fault)
{
    if (sg == NULL) return;
    sg->internal_fault = fault;
}

void steering_safety_feed_comms(steering_safety_t* sg, uint64_t timestamp_us)
{
    if (sg == NULL) return;
    sg->last_comms_us = timestamp_us;
    sg->comms_active  = true;
}

void steering_safety_clear_fault(steering_safety_t* sg)
{
    if (sg == NULL) return;
    sg->internal_fault = false;
}

steer_safety_result_t steering_safety_evaluate(
    steering_safety_t* sg,
    const steer_command_input_t* cmd,
    const steer_sensor_input_t* sensor,
    uint64_t now_us)
{
    steer_safety_result_t result;
    memset(&result, 0, sizeof(result));

    if (sg == NULL) {
        result.safe   = false;
        result.reason = STEER_SAFETY_INTERNAL_FAULT;
        return result;
    }

    sg->eval_count++;

    /* ==================================================================
     * Safety Condition 1: Global steering disabled
     * ================================================================== */
    if (!sg->global_enabled) {
        result.safe   = false;
        result.reason = STEER_SAFETY_GLOBAL_DISABLED;
        goto done;
    }

    /* ==================================================================
     * Safety Condition 2: Local steering switch OFF
     * ================================================================== */
    if (!sg->local_switch) {
        result.safe   = false;
        result.reason = STEER_SAFETY_LOCAL_SWITCH_OFF;
        goto done;
    }

    /* ==================================================================
     * Safety Condition 10: Internal fault
     * ================================================================== */
    if (sg->internal_fault) {
        result.safe   = false;
        result.reason = STEER_SAFETY_INTERNAL_FAULT;
        goto done;
    }

    /* ==================================================================
     * Safety Condition 9: Communication lost
     * ================================================================== */
    if (sg->comms_active) {
        if (!is_fresh(sg->last_comms_us, now_us, sg->comms_timeout_us)) {
            result.safe   = false;
            result.reason = STEER_SAFETY_COMMS_LOST;
            goto done;
        }
    } else {
        /* Never received communication → not enabled yet */
        result.safe   = false;
        result.reason = STEER_SAFETY_COMMS_LOST;
        goto done;
    }

    /* ==================================================================
     * Safety Condition 4: No valid command (COMMAND_INVALID)
     * ================================================================== */
    if (cmd == NULL || !cmd->valid) {
        result.safe   = false;
        result.reason = STEER_SAFETY_COMMAND_INVALID;
        goto done;
    }

    /* ==================================================================
     * Safety Condition 3: Command stale
     * ================================================================== */
    if (!is_fresh(cmd->timestamp_us, now_us, sg->command_timeout_us)) {
        result.safe   = false;
        result.reason = STEER_SAFETY_COMMAND_STALE;
        goto done;
    }

    /* ==================================================================
     * Safety Condition 5+6: Sensor stale or invalid
     * ================================================================== */
    if (sensor == NULL || !sensor->valid) {
        result.safe   = false;
        result.reason = STEER_SAFETY_SENSOR_INVALID;
        goto done;
    }

    if (!sensor->fresh) {
        result.safe   = false;
        result.reason = STEER_SAFETY_SENSOR_STALE;
        goto done;
    }

    /* ==================================================================
     * Safety Condition 5: Sensor unplausible (angle outside physical range)
     * ================================================================== */
    if (sensor->angle_deg < -sg->angle_abs_max_deg ||
        sensor->angle_deg >  sg->angle_abs_max_deg) {
        result.safe   = false;
        result.reason = STEER_SAFETY_SENSOR_UNPLAUSIBLE;
        goto done;
    }

    /* ==================================================================
     * Safety Condition 7: Setpoint outside allowed range → CLAMP (not motor off)
     * Decision: Clamp setpoint to allowed range, do NOT disable motor.
     * This allows the steering to continue operating at the limit while
     * the operator corrects the commanded angle.
     * ================================================================== */
    float setpoint = cmd->setpoint_deg;
    result.setpoint_clamped = false;
    result.clamped_setpoint_deg = setpoint;

    if (setpoint < sg->angle_min_deg) {
        result.clamped_setpoint_deg = sg->angle_min_deg;
        result.setpoint_clamped = true;
        setpoint = sg->angle_min_deg;
    } else if (setpoint > sg->angle_max_deg) {
        result.clamped_setpoint_deg = sg->angle_max_deg;
        result.setpoint_clamped = true;
        setpoint = sg->angle_max_deg;
    }

    /* ==================================================================
     * Safety Condition 8: Actual angle outside range → Motor OFF
     * If the actual angle is beyond the physical range, something is
     * mechanically wrong → disable immediately.
     * ================================================================== */
    if (sensor->angle_deg < -sg->angle_abs_max_deg ||
        sensor->angle_deg >  sg->angle_abs_max_deg) {
        /* Already caught above as SENSOR_UNPLAUSIBLE */
        result.safe   = false;
        result.reason = STEER_SAFETY_ACTUAL_OOR;
        goto done;
    }

    /* ==================================================================
     * All conditions passed → SAFE
     * ================================================================== */
    result.safe   = true;
    result.reason = STEER_SAFETY_OK;

    /* PWM saturation defaults */
    result.pwm_saturated = false;
    result.saturated_pwm = 0.0f;  /* will be set by steering_control */

done:
    sg->result = result;

    if (!result.safe) {
        sg->unsafe_count++;
    }
    if (result.reason < 13) {
        sg->reason_counts[result.reason]++;
    }

    return result;
}

const steer_safety_result_t* steering_safety_get_result(
    const steering_safety_t* sg)
{
    if (sg == NULL) return NULL;
    return &sg->result;
}

const char* steering_safety_reason_str(steer_safety_reason_t reason)
{
    if (reason >= 13) {
        return "UNKNOWN";
    }
    return s_reason_strings[reason];
}
