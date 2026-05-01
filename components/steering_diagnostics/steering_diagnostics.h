#pragma once

/* steering_diagnostics.h — Steering Diagnostics (STEER-MIG-001 AP-A)
 *
 * Collects and publishes steering health/debug information.
 * Reads from steering_control diagnostics snapshot.
 *
 * Provides:
 *   - Steering health state
 *   - Safety status summary
 *   - PID tuning info
 *   - Output statistics
 *   - Error history (last N safety violations)
 *
 * No Arduino, no heap, no HAL, no transport.
 * Depends on: steering_control.h, snapshot_buffer.h
 */

#include <stdint.h>
#include <stdbool.h>

#include "steering_control.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Error history entry ---- */

#define STEER_DIAG_HISTORY_SIZE  16

typedef struct {
    steer_safety_reason_t reason;   /**< Safety reason code */
    uint64_t timestamp_us;          /**< When it occurred */
} steer_error_entry_t;

/* ---- Diagnostics state ---- */

typedef struct {
    /* Safety summary */
    bool safety_ok;
    steer_safety_reason_t safety_reason;
    uint32_t safety_block_count;

    /* PID info */
    float setpoint_deg;
    float actual_deg;
    float error_deg;
    float pid_output;
    float integral;

    /* Output info */
    float motor_pwm;
    bool motor_enabled;
    bool motor_safety_blocked;

    /* Counts */
    uint32_t cycle_count;
    uint32_t total_safety_blocks;

    /* Error history */
    steer_error_entry_t error_history[STEER_DIAG_HISTORY_SIZE];
    uint8_t error_history_count;
    uint8_t error_history_index;

    /* Uptime */
    uint64_t first_cycle_us;
    uint64_t last_cycle_us;
} steering_diagnostics_t;

/* ---- Diagnostics component ---- */

typedef struct {
    /* Input source (NOT owned) */
    const snapshot_buffer_t* diag_source;  /**< steer_diag_snapshot_t */

    /* Diagnostics state */
    steering_diagnostics_t state;

    /* Published diagnostics snapshot */
    snapshot_buffer_t diag_snapshot;
    steering_diagnostics_t diag_storage;
} steering_diagnostics_t_comp;

/* ---- API ---- */

/** Initialize diagnostics component. */
void steering_diagnostics_init(steering_diagnostics_t_comp* diag);

/** Set diagnostics source (steering_control's diag snapshot). */
void steering_diagnostics_set_source(steering_diagnostics_t_comp* diag,
                                      const snapshot_buffer_t* source);

/** Service step: read diagnostics, update error history, publish. */
void steering_diagnostics_service_step(steering_diagnostics_t_comp* diag,
                                        uint64_t timestamp_us);

/** Get diagnostics snapshot buffer. */
const snapshot_buffer_t* steering_diagnostics_get_snapshot(
    const steering_diagnostics_t_comp* diag);

/** Get error history. */
const steer_error_entry_t* steering_diagnostics_get_error_history(
    const steering_diagnostics_t_comp* diag,
    uint8_t* count);

#ifdef __cplusplus
}
#endif
