/* actuator_drv8263h.c — DRV8263H PWM/DIR motor driver (skeleton).
 *
 * Reads a steering_command_t from the bound command snapshot and computes
 * a simulated duty-cycle value. No real GPIO / PWM interaction.
 *
 * Safety coupling:
 *   Reads safety_status_t from the bound safety snapshot.
 *   When safety is triggered, output is functionally disabled:
 *     - duty clamped to 0.0
 *     - safety_blocked flag set
 *     - simulates high-Z / neutral state
 *
 * Enable Policy:
 *   Auto-enables on first valid command with sensors_valid == true.
 *   Auto-disables when no valid command for ACTUATOR_DISABLE_TIMEOUT cycles.
 *   Manual enable/disable still available as override.
 *
 * The actuator does NOT decide about safety independently — it purely
 * consumes the safety snapshot provided by safety_failsafe.
 */

#include "actuator_drv8263h.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/* steering_command_t layout for snapshot access.
 * Duplicated here to avoid pulling in steering_control.h as a dependency
 * (avoids circular CMake REQUIRES). Must stay in sync with steering_control.h. */
typedef struct {
    float steer_angle_deg;        /**< Commanded angle from AOG (setpoint) */
    float steer_angle_actual_deg; /**< Actual angle from WAS (feedback) */
    float speed_ms;               /**< Vehicle speed from AOG */
    float heading_error_deg;      /**< Heading error from IMU (compensation) */
    uint8_t status;               /**< 0 = inactive, 1 = active */
    bool valid;                   /**< True when a valid command is available */
    bool sensors_valid;           /**< True when WAS + IMU data are both valid */
} actuator_steer_cmd_t;

/* ------------------------------------------------------------------ */
/*  Service step                                                        */
/* ------------------------------------------------------------------ */

static void actuator_drv8263h_service_step_fn(runtime_component_t* comp,
                                              uint64_t timestamp_us)
{
    (void)timestamp_us;
    actuator_drv8263h_t* act = (actuator_drv8263h_t*)comp;
    if (act == NULL) {
        return;
    }

    act->update_count++;

    /* ---- 1. Check safety status (highest priority) ---- */
    act->safety_blocked = false;
    if (act->safety_source != NULL) {
        actuator_safety_status_t safety;
        bool ok = snapshot_buffer_get(act->safety_source, &safety);
        if (ok && safety.triggered) {
            /* Safety triggered: functionally disable output. */
            act->safety_blocked = true;
            act->enabled = false;
            act->current_duty = 0.0f;
            return;
        }
    }

    /* ---- 2. Read command snapshot ---- */
    actuator_steer_cmd_t cmd;
    bool cmd_ok = false;

    if (act->command_source != NULL) {
        bool ok = snapshot_buffer_get(act->command_source, &cmd);
        if (ok && cmd.valid) {
            cmd_ok = true;
        }
    }

    /* ---- 3. Auto-enable / auto-disable logic ---- */
    if (cmd_ok) {
        /* Valid command received — reset no-valid counter. */
        act->no_valid_count = 0u;

        /* Auto-enable: first valid command with full sensor backing. */
        if (!act->enabled && cmd.sensors_valid) {
            act->enabled = true;
        }
    } else {
        /* No valid command — increment counter. */
        act->no_valid_count++;
        if (act->no_valid_count >= ACTUATOR_DISABLE_TIMEOUT) {
            act->enabled = false;
        }
    }

    /* ---- 4. Check enable flag ---- */
    if (!act->enabled) {
        act->current_duty = 0.0f;
        return;
    }

    /* ---- 5. Produce duty from command ---- */
    /* Skeleton: map steer_angle_deg directly to duty (-1 … +1).
     * A real implementation would use PID control here,
     * incorporating steer_angle_actual_deg for closed-loop feedback
     * and heading_error_deg for heading compensation. */
    if (cmd.steer_angle_deg > 0.0f) {
        act->current_duty = (cmd.steer_angle_deg > 40.0f) ? 1.0f
                          : (cmd.steer_angle_deg / 40.0f);
    } else {
        act->current_duty = (cmd.steer_angle_deg < -40.0f) ? -1.0f
                          : (cmd.steer_angle_deg / 40.0f);
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

void actuator_drv8263h_init(actuator_drv8263h_t* act,
                            uint8_t pwm_pin,
                            uint8_t dir_pin)
{
    if (act == NULL) {
        return;
    }

    memset(act, 0, sizeof(actuator_drv8263h_t));

    act->component.name         = "actuator_drv8263h";
    act->component.user_data    = act;
    act->component.fast_input   = NULL;
    act->component.fast_process = NULL;
    act->component.fast_output  = NULL;
    act->component.service_step = actuator_drv8263h_service_step_fn;

    act->command_source = NULL;
    act->safety_source  = NULL;
    act->pwm_pin        = pwm_pin;
    act->dir_pin        = dir_pin;

    act->current_duty   = 0.0f;
    act->enabled        = false;   /* starts disabled; auto-enables on valid command */
    act->safety_blocked = false;
    act->update_count   = 0u;
    act->no_valid_count = 0u;
}

void actuator_drv8263h_set_command_source(actuator_drv8263h_t* act,
                                          const snapshot_buffer_t* src)
{
    if (act == NULL) {
        return;
    }
    act->command_source = src;
}

void actuator_drv8263h_set_safety_source(actuator_drv8263h_t* act,
                                         const snapshot_buffer_t* src)
{
    if (act == NULL) {
        return;
    }
    act->safety_source = src;
}

void actuator_drv8263h_service_step(runtime_component_t* comp,
                                    uint64_t timestamp_us)
{
    actuator_drv8263h_service_step_fn(comp, timestamp_us);
}

float actuator_drv8263h_get_duty(const actuator_drv8263h_t* act)
{
    if (act == NULL) {
        return 0.0f;
    }
    return act->current_duty;
}

bool actuator_drv8263h_is_safety_blocked(const actuator_drv8263h_t* act)
{
    if (act == NULL) {
        return true;  /* fail-safe */
    }
    return act->safety_blocked;
}

bool actuator_drv8263h_is_enabled(const actuator_drv8263h_t* act)
{
    if (act == NULL) {
        return false;
    }
    return act->enabled;
}

void actuator_drv8263h_enable(actuator_drv8263h_t* act)
{
    if (act == NULL) {
        return;
    }
    act->enabled = true;
}

void actuator_drv8263h_disable(actuator_drv8263h_t* act)
{
    if (act == NULL) {
        return;
    }
    act->enabled = false;
    act->current_duty = 0.0f;
}
