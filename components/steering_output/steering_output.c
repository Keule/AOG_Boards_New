/* steering_output.c — Motor/PWM Output Implementation (STEER-MIG-001 AP-E)
 *
 * Translates normalized steering command to HAL motor output.
 * Safety-first: any safety violation → motor OFF.
 *
 * No direct HAL GPIO/PWM in business logic — all through steering_output_hal_t.
 */

#include "steering_output.h"
#include <string.h>
#include <math.h>

/* ---- Internal: apply deadzone ---- */

static float apply_deadzone(float value, float deadzone)
{
    if (deadzone <= 0.0f) return value;
    if (fabsf(value) < deadzone) return 0.0f;
    /* Remap: after deadzone, linearly scale to full range */
    if (value > 0.0f) {
        return (value - deadzone) / (1.0f - deadzone);
    } else {
        return (value + deadzone) / (1.0f - deadzone);
    }
}

/* ---- Internal: saturate to [-1, +1] ---- */

static float saturate(float value, float min_val, float max_val)
{
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

/* ---- Internal: write to HAL ---- */

static void write_hal(steering_output_t* out)
{
    if (out->hal == NULL) return;

    if (out->state.safety_blocked || !out->state.motor_enabled) {
        /* Motor OFF: force all outputs to safe state */
        out->hal->set_enable(false);
        out->hal->set_pwm(0.0f);
        out->hal->last_enabled = false;
        out->hal->last_pwm = 0.0f;
        return;
    }

    /* Normal operation */
    out->hal->set_enable(true);
    out->hal->set_direction(out->state.direction);

    /* PWM: always positive, magnitude only */
    float abs_pwm = fabsf(out->state.pwm);
    abs_pwm = saturate(abs_pwm, out->pwm_min, out->pwm_max);
    out->hal->set_pwm(abs_pwm);

    out->hal->last_enabled = true;
    out->hal->last_pwm = abs_pwm;
    out->hal->last_direction = out->state.direction;

    out->output_count++;
}

/* ---- Public API ---- */

void steering_output_init(steering_output_t* out, steering_output_hal_t* hal)
{
    if (out == NULL) return;

    memset(out, 0, sizeof(steering_output_t));

    out->hal = hal;

    /* Conservative defaults */
    out->pwm_deadzone = 0.05f;
    out->pwm_min      = 0.0f;
    out->pwm_max      = 1.0f;

    /* Default: motor OFF */
    out->state.motor_enabled  = false;
    out->state.pwm            = 0.0f;
    out->state.direction      = false;
    out->state.safety_blocked = true;  /* blocked until safety says OK */
}

void steering_output_set_deadzone(steering_output_t* out, float deadzone)
{
    if (out == NULL) return;
    if (deadzone < 0.0f) deadzone = 0.0f;
    if (deadzone > 0.5f) deadzone = 0.5f;
    out->pwm_deadzone = deadzone;
}

void steering_output_set_limits(steering_output_t* out, float min, float max)
{
    if (out == NULL) return;
    out->pwm_min = min;
    out->pwm_max = max;
}

void steering_output_update(steering_output_t* out,
                             float command_pwm,
                             bool safety_ok,
                             uint64_t now_us)
{
    (void)now_us;
    if (out == NULL) return;

    out->update_count++;

    /* ---- Safety gate ---- */
    if (!safety_ok) {
        out->state.safety_blocked = true;
        out->state.motor_enabled  = false;
        out->state.pwm            = 0.0f;
        out->safety_block_count++;
        write_hal(out);
        return;
    }

    out->state.safety_blocked = false;

    /* ---- Apply deadzone ---- */
    float processed = apply_deadzone(command_pwm, out->pwm_deadzone);

    /* ---- Saturate to [-1, +1] ---- */
    processed = saturate(processed, -1.0f, 1.0f);

    /* ---- NaN / Inf guard ---- */
    if (!isfinite(processed)) {
        processed = 0.0f;
    }

    /* ---- Determine direction ---- */
    out->state.direction = (processed >= 0.0f);
    out->state.pwm       = processed;
    out->state.motor_enabled = true;

    write_hal(out);
}

void steering_output_force_off(steering_output_t* out)
{
    if (out == NULL) return;

    out->state.motor_enabled  = false;
    out->state.pwm            = 0.0f;
    out->state.direction      = false;
    out->state.safety_blocked = true;

    write_hal(out);

    if (out->hal != NULL && out->hal->emergency_stop != NULL) {
        out->hal->emergency_stop();
    }
}

const steering_output_state_t* steering_output_get_state(
    const steering_output_t* out)
{
    if (out == NULL) return NULL;
    return &out->state;
}

/* ---- Mock HAL for tests ---- */

static void mock_set_enable(bool enabled)    { (void)enabled; }
static void mock_set_direction(bool dir)     { (void)dir; }
static void mock_set_pwm(float duty)         { (void)duty; }
static void mock_emergency_stop(void)        { }

void steering_output_mock_hal_init(steering_output_hal_t* hal)
{
    if (hal == NULL) return;

    memset(hal, 0, sizeof(steering_output_hal_t));

    hal->set_enable       = mock_set_enable;
    hal->set_direction    = mock_set_direction;
    hal->set_pwm          = mock_set_pwm;
    hal->emergency_stop   = mock_emergency_stop;

    hal->last_enabled     = false;
    hal->last_direction   = false;
    hal->last_pwm         = 0.0f;
}
