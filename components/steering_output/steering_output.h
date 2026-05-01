#pragma once

/* steering_output.h — Motor/PWM Output Abstraction (STEER-MIG-001 AP-E)
 *
 * Takes a normalized steering command and translates it to motor output
 * via a HAL abstraction. Does NOT know GPIO details directly.
 *
 * Safety coupling:
 *   When steering_safety says NOT safe, this component forces motor OFF:
 *   - PWM = 0, Motor disabled, Direction = neutral
 *
 * Mockable:
 *   steering_output_hal_t interface allows replacing HAL with mock in tests.
 *
 * No Arduino, no direct HAL GPIO/PWM calls in business logic.
 * Depends on: runtime_types.h
 */

#include <stdint.h>
#include <stdbool.h>

#include "runtime_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Output state ---- */

typedef struct {
    float pwm;               /**< Normalized PWM output (-1.0 to +1.0) */
    bool motor_enabled;      /**< Motor enable relay state */
    bool direction;          /**< Direction: false=left, true=right */
    bool safety_blocked;     /**< True when output is blocked by safety */
} steering_output_state_t;

/* ---- HAL abstraction for motor output (mockable) ---- */

typedef struct {
    /* Set motor enable relay (true=on, false=off) */
    void (*set_enable)(bool enabled);

    /* Set motor direction (false=left/CCW, true=right/CW) */
    void (*set_direction)(bool right);

    /* Set PWM duty cycle (0.0 to 1.0, always positive) */
    void (*set_pwm)(float duty);

    /* Emergency stop: force all outputs to safe state */
    void (*emergency_stop)(void);

    /* Get last set values (for test inspection) */
    float last_pwm;
    bool last_enabled;
    bool last_direction;
} steering_output_hal_t;

/* ---- Main output component ---- */

typedef struct {
    /* HAL interface (NOT owned, set by caller) */
    steering_output_hal_t* hal;

    /* Configuration */
    float pwm_deadzone;       /**< Deadzone around zero (e.g. 0.05 = 5%) */
    float pwm_min;            /**< Minimum PWM after deadzone */
    float pwm_max;            /**< Maximum PWM */

    /* Current state */
    steering_output_state_t state;

    /* Statistics */
    uint32_t update_count;
    uint32_t safety_block_count;
    uint32_t output_count;    /**< Number of actual output writes */
} steering_output_t;

/* ---- API ---- */

/** Initialize output component.
 *  Default state: motor OFF, PWM=0, safety blocked.
 *  If hal is NULL, output will track state but not write hardware. */
void steering_output_init(steering_output_t* out, steering_output_hal_t* hal);

/** Set PWM deadzone. Commands within [-deadzone, +deadzone] produce zero PWM. */
void steering_output_set_deadzone(steering_output_t* out, float deadzone);

/** Set PWM limits (after deadzone). */
void steering_output_set_limits(steering_output_t* out, float min, float max);

/** Update output from normalized command and safety state.
 *  This is the FAST_OUTPUT hook.
 *
 *  @param command_pwm  Normalized steering command (-1.0 to +1.0).
 *                      Positive = steer right, negative = steer left.
 *  @param safety_ok    True when safety gate allows motor output.
 *  @param now_us       Current timestamp (for statistics). */
void steering_output_update(steering_output_t* out,
                             float command_pwm,
                             bool safety_ok,
                             uint64_t now_us);

/** Force motor OFF immediately. Independent of safety state. */
void steering_output_force_off(steering_output_t* out);

/** Get current output state (for diagnostics / test inspection). */
const steering_output_state_t* steering_output_get_state(
    const steering_output_t* out);

/** Create a mock HAL with inspection capability (for tests). */
void steering_output_mock_hal_init(steering_output_hal_t* hal);

#ifdef __cplusplus
}
#endif
