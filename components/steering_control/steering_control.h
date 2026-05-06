#pragma once

/* steering_control.h — Steering Controller (STEER-MIG-001 AP-B/C)
 *
 * Reads command and sensor snapshots, evaluates safety, computes
 * PID output, and drives the motor via steering_output.
 *
 * Fast-path integration:
 *   fast_input:  read command + sensor snapshots, read enable switches
 *   fast_process: safety gate evaluation, PID computation, saturation
 *   fast_output: motor output via steering_output, diagnostics
 *
 * Depends on: runtime_component.h, runtime_types.h, snapshot_buffer.h,
 *             steering_safety.h, steering_output.h
 *
 * This component does NOT:
 *   - call HAL functions directly (via steering_output HAL)
 *   - call transport functions
 *   - parse AOG frames (aog_steering_app does that)
 *   - use heap allocation
 */

#include <stdint.h>
#include <stdbool.h>

#include "runtime_component.h"
#include "runtime_types.h"
#include "snapshot_buffer.h"
#include "aog_pgn.h"
#include "steering_safety.h"
#include "steering_output.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- PID Configuration ---- */

typedef struct {
    float kp;          /**< Proportional gain */
    float ki;          /**< Integral gain */
    float kd;          /**< Derivative gain */
    float integral_max; /**< Anti-windup: max integral accumulation */
    float output_min;  /**< Minimum output (PWM) */
    float output_max;  /**< Maximum output (PWM) */
    uint64_t dt_us;    /**< Cycle time in microseconds (10000 = 10ms @100Hz) */
} steer_pid_config_t;

/* ---- Diagnostics snapshot ---- */

typedef struct {
    float setpoint_deg;          /**< Current setpoint (after clamping) */
    float actual_deg;            /**< Current actual angle from WAS */
    float error_deg;             /**< Current error (setpoint - actual) */
    float pid_output;            /**< Raw PID output before saturation */
    float saturated_output;      /**< Output after saturation */
    float integral;              /**< Current integral accumulator */
    bool safety_ok;              /**< Safety gate result */
    steer_safety_reason_t safety_reason;  /**< Safety reason code */
    uint64_t timestamp_us;       /**< Timestamp of this evaluation */
    uint32_t cycle_count;        /**< Total fast-path cycles */
    uint32_t safety_block_count; /**< Total safety blocks */
} steer_diag_snapshot_t;

/* ---- Main steering controller struct ---- */

typedef struct {
    runtime_component_t component;    /**< MUST be first field */

    /* Input sources (NOT owned, set by caller) */
    const snapshot_buffer_t* steer_input_source;  /**< aog_steer_input_t from aog_steering_app */
    const snapshot_buffer_t* was_source;           /**< was_sensor_data_t from was_sensor */

    /* Safety gate */
    steering_safety_t safety;

    /* Motor output */
    steering_output_t output;

    /* PID controller state */
    steer_pid_config_t pid_config;
    float integral;
    float prev_error;
    bool pid_initialized;

    /* Command tracking for freshness */
    uint64_t last_command_timestamp_us;

    /* Diagnostics */
    steer_diag_snapshot_t diag;
    snapshot_buffer_t diag_snapshot;
    steer_diag_snapshot_t diag_storage;

    /* Statistics */
    uint32_t fast_cycle_count;
} steering_control_t;

/* ---- API ---- */

/** Initialize steering controller.
 *  Sets up safety gate (OFF), motor output (OFF), PID (defaults).
 *  Registers fast_input, fast_process, fast_output hooks. */
void steering_control_init(steering_control_t* ctrl);

/** Bind the SteeringInput snapshot source (NOT owned).
 *  Source is aog_steer_input_t from aog_steering_app. */
void steering_control_set_steer_input(steering_control_t* ctrl,
                                      const snapshot_buffer_t* source);

/** Bind the WAS snapshot source (NOT owned).
 *  Source is was_sensor_data_t from was_sensor. */
void steering_control_set_was(steering_control_t* ctrl,
                              const snapshot_buffer_t* was);

/** Set PID configuration. */
void steering_control_set_pid(steering_control_t* ctrl,
                              float kp, float ki, float kd,
                              float integral_max,
                              float output_min, float output_max);

/** Configure safety gate timeouts. */
void steering_control_set_safety_timeouts(steering_control_t* ctrl,
                                           uint64_t command_us,
                                           uint64_t sensor_us,
                                           uint64_t comms_us);

/** Set global steering enable. Must be called to enable steering. */
void steering_control_set_global_enabled(steering_control_t* ctrl, bool enabled);

/** Set local steering switch. */
void steering_control_set_local_switch(steering_control_t* ctrl, bool on);

/** Set motor output HAL (for test injection). */
void steering_control_set_output_hal(steering_control_t* ctrl,
                                      steering_output_hal_t* hal);

/** Set motor output deadzone. */
void steering_control_set_output_deadzone(steering_control_t* ctrl, float deadzone);

/** Fast input hook: read command + sensor snapshots. */
void steering_control_fast_input(runtime_component_t* comp,
                                  const fast_cycle_context_t* ctx);

/** Fast process hook: safety gate + PID computation. */
void steering_control_fast_process(runtime_component_t* comp,
                                    const fast_cycle_context_t* ctx);

/** Fast output hook: motor output + diagnostics. */
void steering_control_fast_output(runtime_component_t* comp,
                                   const fast_cycle_context_t* ctx);

/** Service step (fallback, delegates to fast hooks with synthetic context). */
void steering_control_service_step(runtime_component_t* comp,
                                   uint64_t timestamp_us);

/** Get diagnostics snapshot buffer (for external consumption). */
const snapshot_buffer_t* steering_control_get_diag_snapshot(
    const steering_control_t* ctrl);

/** Get safety gate reference (for external inspection). */
const steering_safety_t* steering_control_get_safety(
    const steering_control_t* ctrl);

/** Get output reference (for external inspection). */
const steering_output_t* steering_control_get_output(
    const steering_control_t* ctrl);

#ifdef __cplusplus
}
#endif
