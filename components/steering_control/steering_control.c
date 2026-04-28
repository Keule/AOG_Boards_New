/* steering_control.c — AOG steering command processor.
 *
 * Reads the SteeringInput snapshot produced by aog_steering_app,
 * combines with WAS and IMU data, and publishes a SteeringCommand
 * snapshot for the actuator layer.
 *
 * Sensor integration:
 *   - WAS: reads actual wheel angle for closed-loop feedback.
 *     Computes heading_error = setpoint - actual as command correction.
 *   - IMU: reads heading/roll/yawrate for status validation and
 *     future heading compensation.
 *   - Sensors must be valid for the command to carry sensors_valid=true.
 *
 * Safety coupling:
 *   When a valid command with valid sensors is produced, feeds the
 *   safety watchdog via safety_failsafe_feed(). If no valid command
 *   or sensor data is available, the watchdog is NOT fed, causing
 *   a timeout-triggered failsafe.
 *
 * This component does NOT access AOG RX buffers or parse AOG frames.
 */

#include "steering_control.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Service step                                                        */
/* ------------------------------------------------------------------ */

static void steering_control_service_step_fn(runtime_component_t* comp,
                                             uint64_t timestamp_us)
{
    steering_control_t* ctrl = (steering_control_t*)comp;
    if (ctrl == NULL) {
        return;
    }

    bool steer_input_ok = false;
    aog_steer_input_t steer_in;

    /* ---- 1. Read WAS snapshot (closed-loop feedback) ---- */
    ctrl->was_valid = false;
    if (ctrl->was_source != NULL) {
        steer_was_data_t was;
        bool ok = snapshot_buffer_get(ctrl->was_source, &was);
        if (ok && was.valid) {
            ctrl->was_data     = was;
            ctrl->was_valid    = true;
        }
    }

    /* ---- 2. Read IMU snapshot (heading compensation / status) ---- */
    ctrl->imu_valid = false;
    if (ctrl->imu_source != NULL) {
        steer_imu_data_t imu;
        bool ok = snapshot_buffer_get(ctrl->imu_source, &imu);
        if (ok && imu.valid) {
            ctrl->imu_data     = imu;
            ctrl->imu_valid    = true;
        }
    }

    /* ---- 3. Read SteeringInput snapshot from aog_steering_app ---- */
    if (ctrl->steer_input_source != NULL) {
        bool ok = snapshot_buffer_get(ctrl->steer_input_source, &steer_in);
        if (ok) {
            steer_input_ok = true;
        }
    }

    /* ---- 4. Form command ---- */
    if (!steer_input_ok) {
        /* No valid AOG input → no command, do NOT feed safety watchdog. */
        ctrl->command.valid         = false;
        ctrl->command.status        = 0u;
        ctrl->command.sensors_valid = false;
        snapshot_buffer_set(&ctrl->command_snapshot, &ctrl->command);
        return;
    }

    /* Valid AOG input available — build command. */
    ctrl->command.steer_angle_deg = steer_in.steer_angle_deg;
    ctrl->command.speed_ms        = steer_in.speed_ms;

    /* WAS feedback: compute actual angle and error. */
    if (ctrl->was_valid) {
        ctrl->command.steer_angle_actual_deg = ctrl->was_data.degrees;
        /* Error: positive = need to steer right, negative = steer left. */
        /* In skeleton: pass raw error; real PID would use this. */
    } else {
        ctrl->command.steer_angle_actual_deg = 0.0f;
    }

    /* IMU: heading compensation (skeleton: pass 0, real impl would compute).
     * Roll and yawrate are available in ctrl->imu_data for future use. */
    ctrl->command.heading_error_deg = 0.0f;

    /* Combined sensor validity: both WAS and IMU must be valid
     * for the command to be considered fully sensor-backed. */
    ctrl->command.sensors_valid = (ctrl->was_valid && ctrl->imu_valid);
    ctrl->command.status        = 1u;  /* active */
    ctrl->command.valid         = true;

    /* ---- 5. Publish command snapshot ---- */
    snapshot_buffer_set(&ctrl->command_snapshot, &ctrl->command);

    /* ---- 6. Feed safety watchdog when operating normally ---- */
    if (ctrl->safety_target != NULL && ctrl->command.sensors_valid) {
        safety_failsafe_feed(ctrl->safety_target, timestamp_us);
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

void steering_control_init(steering_control_t* ctrl)
{
    if (ctrl == NULL) {
        return;
    }

    memset(ctrl, 0, sizeof(steering_control_t));

    ctrl->component.service_step = steering_control_service_step_fn;

    snapshot_buffer_init(&ctrl->command_snapshot,
                         &ctrl->command_storage,
                         sizeof(steering_command_t));
}

void steering_control_set_steer_input(steering_control_t* ctrl,
                                      const snapshot_buffer_t* source)
{
    if (ctrl == NULL) {
        return;
    }
    ctrl->steer_input_source = source;
}

void steering_control_set_was(steering_control_t* ctrl,
                              const snapshot_buffer_t* was)
{
    if (ctrl == NULL) {
        return;
    }
    ctrl->was_source = was;
}

void steering_control_set_imu(steering_control_t* ctrl,
                              const snapshot_buffer_t* imu)
{
    if (ctrl == NULL) {
        return;
    }
    ctrl->imu_source = imu;
}

void steering_control_set_safety_target(steering_control_t* ctrl,
                                        safety_failsafe_t* sf)
{
    if (ctrl == NULL) {
        return;
    }
    ctrl->safety_target = sf;
}

void steering_control_service_step(runtime_component_t* comp,
                                   uint64_t timestamp_us)
{
    steering_control_service_step_fn(comp, timestamp_us);
}

const snapshot_buffer_t* steering_control_get_command_snapshot(
    const steering_control_t* ctrl)
{
    if (ctrl == NULL) {
        return NULL;
    }
    return &ctrl->command_snapshot;
}
