#pragma once

/* steering_control.h — AOG steering command processor.
 *
 * Consumes:
 *   - SteeringInput snapshot (from aog_steering_app via snapshot_buffer_t)
 *   - WAS snapshot (from was_sensor)
 *   - IMU snapshot (from imu_bno085)
 *
 * Produces:
 *   - steering_command_t snapshot for the actuator layer
 *   - safety_failsafe_feed() when a valid command with valid sensors is produced
 *
 * This component does NOT:
 *   - read AOG RX directly (aog_steering_app owns parsing)
 *   - parse AOG frames
 *   - call transport functions
 *   - call HAL functions
 *   - use heap allocation
 *
 * No Arduino, no heap, no HAL, no transport.
 * Depends on: runtime_component.h, snapshot_buffer.h, aog_pgn.h
 */

#include <stdint.h>
#include <stdbool.h>

#include "runtime_component.h"
#include "snapshot_buffer.h"
#include "aog_pgn.h"
#include "safety_failsafe.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- WAS sensor data layout (must match was_sensor.h).
 * Duplicated here to avoid pulling in was_sensor.h as a dependency. */
typedef struct {
    uint16_t raw;       /**< ADC raw reading */
    float degrees;      /**< Calibrated angle in degrees */
    bool valid;         /**< True when conversion succeeded */
} steer_was_data_t;

/* ---- IMU data layout (must match imu_bno085.h).
 * Duplicated here to avoid pulling in imu_bno085.h as a dependency. */
typedef struct {
    double heading_rad;
    double roll_rad;
    double yawrate_rad_s;
    bool valid;
} steer_imu_data_t;

/* ---- Steering command (output) ---- */

typedef struct {
    float steer_angle_deg;        /**< Commanded angle from AOG (setpoint) */
    float steer_angle_actual_deg; /**< Actual angle from WAS (feedback) */
    float speed_ms;               /**< Vehicle speed from AOG */
    float heading_error_deg;      /**< Heading error from IMU (compensation) */
    uint8_t status;               /**< 0 = inactive, 1 = active */
    bool valid;                   /**< True when a valid command is available */
    bool sensors_valid;           /**< True when WAS + IMU data are both valid */
} steering_command_t;

/* ---- Main component struct ---- */

typedef struct {
    runtime_component_t component;    /**< MUST be first field */

    /* Input sources (NOT owned, set by caller) */
    const snapshot_buffer_t* steer_input_source;  /**< SteeringInput from aog_steering_app */
    const snapshot_buffer_t* was_source;           /**< was_sensor snapshot */
    const snapshot_buffer_t* imu_source;           /**< imu_bno085 snapshot */

    /* Safety feed target (NOT owned, set by caller).
     * When a valid command with valid sensors is produced,
     * steering_control feeds this watchdog instance. */
    safety_failsafe_t* safety_target;

    /* Cached sensor data (from last service_step) */
    steer_was_data_t was_data;
    steer_imu_data_t imu_data;
    bool was_valid;
    bool imu_valid;

    /* Command being built (written during service_step) */
    steering_command_t command;

    /* Snapshot for actuator consumption */
    snapshot_buffer_t command_snapshot;
    steering_command_t command_storage;
} steering_control_t;

/* ---- API ---- */

/** Zero-initialise the component and set its service_step callback. */
void steering_control_init(steering_control_t* ctrl);

/** Bind the SteeringInput snapshot source (NOT owned).
 *  This is the ONLY AOG data input — no direct RX buffer access. */
void steering_control_set_steer_input(steering_control_t* ctrl,
                                      const snapshot_buffer_t* source);

/** Bind the WAS snapshot source (NOT owned). */
void steering_control_set_was(steering_control_t* ctrl,
                              const snapshot_buffer_t* was);

/** Bind the IMU snapshot source (NOT owned). */
void steering_control_set_imu(steering_control_t* ctrl,
                              const snapshot_buffer_t* imu);

/** Bind the safety failsafe target (NOT owned).
 *  steering_control will call safety_failsafe_feed() on this instance
 *  whenever a valid command with valid sensors is produced. */
void steering_control_set_safety_target(steering_control_t* ctrl,
                                        safety_failsafe_t* sf);

/** Service-step callback (also callable directly). */
void steering_control_service_step(runtime_component_t* comp,
                                   uint64_t timestamp_us);

/** Return a read-only pointer to the command snapshot. */
const snapshot_buffer_t* steering_control_get_command_snapshot(
    const steering_control_t* ctrl);

#ifdef __cplusplus
}
#endif
