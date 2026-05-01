/* steering_control.c — Steering Controller Implementation (STEER-MIG-001 AP-B/C)
 *
 * Three-phase fast-path:
 *   fast_input:   read command + sensor snapshots
 *   fast_process: safety gate evaluation, PID computation
 *   fast_output:  motor output via HAL, diagnostics update
 *
 * PID controller:
 *   error = setpoint - actual
 *   P = Kp * error
 *   I += Ki * error * dt  (with anti-windup)
 *   D = Kd * (error - prev_error) / dt
 *   output = P + I + D
 *   output = saturate(output, output_min, output_max)
 *
 * Safety-first:
 *   If safety gate says NOT safe → motor OFF, PID reset.
 *   Every unset/missing condition defaults to motor OFF.
 */

#include "steering_control.h"
#include <string.h>
#include <math.h>

/* ---- Internal: snapshot data types for reading ---- */

/* was_sensor_data_t from was_sensor.h (duplicated to avoid circular deps) */
typedef struct {
    uint16_t raw;
    float voltage;
    float degrees;
    bool valid;
    bool fresh;
    uint64_t timestamp_us;
    uint8_t reason;
} ctrl_was_data_t;

/* aog_steer_input_t from aog_pgn.h (referenced via include) */

/* ---- Internal: evaluate WAS freshness for this cycle ---- */

static bool eval_was_freshness(const was_sensor_data_t* was_data,
                                uint64_t now_us,
                                uint64_t timeout_us)
{
    if (was_data == NULL) return false;
    if (!was_data->valid) return false;

    /* Use the snapshot's own fresh flag if available */
    if (was_data->timestamp_us > 0 && timeout_us > 0) {
        if (now_us < was_data->timestamp_us) return false;
        return (now_us - was_data->timestamp_us) <= timeout_us;
    }

    return was_data->fresh;
}

/* ---- Internal: compute PID ---- */

static float compute_pid(steering_control_t* ctrl,
                          float setpoint_deg, float actual_deg,
                          float dt_sec)
{
    float error = setpoint_deg - actual_deg;

    /* Proportional */
    float p_term = ctrl->pid_config.kp * error;

    /* Integral with anti-windup */
    ctrl->integral += ctrl->pid_config.ki * error * dt_sec;
    if (ctrl->integral > ctrl->pid_config.integral_max) {
        ctrl->integral = ctrl->pid_config.integral_max;
    } else if (ctrl->integral < -ctrl->pid_config.integral_max) {
        ctrl->integral = -ctrl->pid_config.integral_max;
    }
    float i_term = ctrl->integral;

    /* Derivative */
    float d_term = 0.0f;
    if (ctrl->pid_initialized) {
        float deriv = (error - ctrl->prev_error) / dt_sec;
        d_term = ctrl->pid_config.kd * deriv;
    }
    ctrl->prev_error = error;
    ctrl->pid_initialized = true;

    float output = p_term + i_term + d_term;

    /* NaN / Inf guard */
    if (!isfinite(output)) {
        output = 0.0f;
        ctrl->integral = 0.0f;
    }

    return output;
}

/* ---- Internal: reset PID state (on safety block) ---- */

static void reset_pid(steering_control_t* ctrl)
{
    ctrl->integral = 0.0f;
    ctrl->prev_error = 0.0f;
    ctrl->pid_initialized = false;
}

/* ==================================================================
 * FAST_INPUT: Read command + sensor snapshots
 * ================================================================== */

static aog_steer_input_t s_cmd;       /* cached command */
static ctrl_was_data_t s_was;         /* cached sensor */
static bool s_cmd_read;               /* command available this cycle */
static bool s_was_read;               /* sensor available this cycle */
static uint64_t s_cmd_ts;             /* command timestamp */
static uint64_t s_was_ts;             /* sensor timestamp */

void steering_control_fast_input(runtime_component_t* comp,
                                  const fast_cycle_context_t* ctx)
{
    (void)ctx;
    steering_control_t* ctrl = (steering_control_t*)comp;
    if (ctrl == NULL) return;

    ctrl->fast_cycle_count++;

    /* ---- Read command snapshot ---- */
    s_cmd_read = false;
    s_cmd_ts = 0;
    if (ctrl->steer_input_source != NULL) {
        if (snapshot_buffer_get(ctrl->steer_input_source, &s_cmd)) {
            s_cmd_read = true;
            s_cmd_ts = ctx->timestamp_us;
        }
    }

    /* ---- Read WAS sensor snapshot ---- */
    s_was_read = false;
    s_was_ts = 0;
    if (ctrl->was_source != NULL) {
        /* Read was_sensor_data_t from snapshot */
        was_sensor_data_t was_snap;
        if (snapshot_buffer_get(ctrl->was_source, &was_snap)) {
            memcpy(&s_was, &was_snap, sizeof(ctrl_was_data_t));
            s_was_read = true;
            s_was_ts = was_snap.timestamp_us;
        }
    }
}

/* ==================================================================
 * FAST_PROCESS: Safety gate + PID computation
 * ================================================================== */

/* Persistent state across fast_process → fast_output */
static bool s_safety_ok;
static float s_pid_output;
static float s_clamped_setpoint;

void steering_control_fast_process(runtime_component_t* comp,
                                    const fast_cycle_context_t* ctx)
{
    steering_control_t* ctrl = (steering_control_t*)comp;
    if (ctrl == NULL) return;

    uint64_t now_us = ctx->timestamp_us;

    /* ---- Build safety inputs ---- */
    steer_command_input_t cmd_input;
    memset(&cmd_input, 0, sizeof(cmd_input));

    if (s_cmd_read) {
        cmd_input.setpoint_deg = s_cmd.steer_angle_deg;
        cmd_input.speed_ms     = s_cmd.speed_ms;
        cmd_input.status       = 1;  /* active */
        cmd_input.valid        = true;
        cmd_input.sensors_valid = true;  /* we'll check WAS separately */
        cmd_input.timestamp_us = s_cmd_ts;
    }

    steer_sensor_input_t sensor_input;
    memset(&sensor_input, 0, sizeof(sensor_input));

    if (s_was_read) {
        sensor_input.angle_deg    = s_was.degrees;
        sensor_input.valid        = s_was.valid;
        sensor_input.fresh        = eval_was_freshness(
            (const was_sensor_data_t*)&s_was,
            now_us,
            ctrl->safety.sensor_timeout_us);
        sensor_input.timestamp_us = s_was_ts;
    }

    /* ---- Update command timestamp for safety comms tracking ---- */
    if (s_cmd_read) {
        ctrl->last_command_timestamp_us = s_cmd_ts;
    }

    /* ---- Evaluate safety gate ---- */
    steer_safety_result_t safety_result = steering_safety_evaluate(
        &ctrl->safety, &cmd_input, &sensor_input, now_us);

    s_safety_ok = safety_result.safe;
    s_clamped_setpoint = safety_result.clamped_setpoint_deg;

    /* ---- Compute PID if safe ---- */
    if (safety_result.safe) {
        float dt_sec = (float)ctrl->pid_config.dt_us / 1000000.0f;
        s_pid_output = compute_pid(ctrl,
                                    safety_result.clamped_setpoint_deg,
                                    s_was.degrees,
                                    dt_sec);

        /* Saturate PID output to PWM range */
        s_pid_output = fmaxf(ctrl->pid_config.output_min, s_pid_output);
        s_pid_output = fminf(ctrl->pid_config.output_max, s_pid_output);
    } else {
        /* Safety blocked → reset PID, zero output */
        s_pid_output = 0.0f;
        reset_pid(ctrl);
    }

    /* ---- Update diagnostics ---- */
    ctrl->diag.setpoint_deg       = safety_result.clamped_setpoint_deg;
    ctrl->diag.actual_deg         = s_was_read ? s_was.degrees : 0.0f;
    ctrl->diag.error_deg          = safety_result.clamped_setpoint_deg -
                                     (s_was_read ? s_was.degrees : 0.0f);
    ctrl->diag.pid_output         = s_pid_output;
    ctrl->diag.saturated_output   = s_pid_output;
    ctrl->diag.integral           = ctrl->integral;
    ctrl->diag.safety_ok          = s_safety_ok;
    ctrl->diag.safety_reason      = safety_result.reason;
    ctrl->diag.timestamp_us       = now_us;
    ctrl->diag.cycle_count        = ctrl->fast_cycle_count;
    ctrl->diag.safety_block_count = ctrl->safety.unsafe_count;
}

/* ==================================================================
 * FAST_OUTPUT: Motor output + diagnostics
 * ================================================================== */

void steering_control_fast_output(runtime_component_t* comp,
                                   const fast_cycle_context_t* ctx)
{
    (void)ctx;
    steering_control_t* ctrl = (steering_control_t*)comp;
    if (ctrl == NULL) return;

    /* ---- Drive motor output ---- */
    steering_output_update(&ctrl->output, s_pid_output, s_safety_ok,
                            ctrl->diag.timestamp_us);

    /* ---- Publish diagnostics snapshot ---- */
    snapshot_buffer_set(&ctrl->diag_snapshot, &ctrl->diag);
}

/* ==================================================================
 * PUBLIC API
 * ================================================================== */

void steering_control_init(steering_control_t* ctrl)
{
    if (ctrl == NULL) return;

    memset(ctrl, 0, sizeof(steering_control_t));

    /* Safety gate: OFF by default */
    steering_safety_init(&ctrl->safety);

    /* Motor output: OFF, no HAL by default */
    steering_output_init(&ctrl->output, NULL);

    /* PID defaults: moderate P, low I, zero D */
    ctrl->pid_config.kp           = 0.8f;
    ctrl->pid_config.ki           = 0.01f;
    ctrl->pid_config.kd           = 0.0f;
    ctrl->pid_config.integral_max = 10.0f;
    ctrl->pid_config.output_min   = -1.0f;
    ctrl->pid_config.output_max   = 1.0f;
    ctrl->pid_config.dt_us        = 10000;  /* 10ms @ 100Hz */

    reset_pid(ctrl);

    /* Init diagnostics snapshot */
    snapshot_buffer_init(&ctrl->diag_snapshot, &ctrl->diag_storage,
                         sizeof(steer_diag_snapshot_t));

    /* Register fast-path hooks */
    ctrl->component.fast_input   = steering_control_fast_input;
    ctrl->component.fast_process = steering_control_fast_process;
    ctrl->component.fast_output  = steering_control_fast_output;
    ctrl->component.service_step = steering_control_service_step;

    /* Init cached state */
    s_safety_ok = false;
    s_pid_output = 0.0f;
    s_clamped_setpoint = 0.0f;
}

void steering_control_set_steer_input(steering_control_t* ctrl,
                                      const snapshot_buffer_t* source)
{
    if (ctrl == NULL) return;
    ctrl->steer_input_source = source;
}

void steering_control_set_was(steering_control_t* ctrl,
                              const snapshot_buffer_t* was)
{
    if (ctrl == NULL) return;
    ctrl->was_source = was;
}

void steering_control_set_pid(steering_control_t* ctrl,
                              float kp, float ki, float kd,
                              float integral_max,
                              float output_min, float output_max)
{
    if (ctrl == NULL) return;
    ctrl->pid_config.kp           = kp;
    ctrl->pid_config.ki           = ki;
    ctrl->pid_config.kd           = kd;
    ctrl->pid_config.integral_max = integral_max;
    ctrl->pid_config.output_min   = output_min;
    ctrl->pid_config.output_max   = output_max;
}

void steering_control_set_safety_timeouts(steering_control_t* ctrl,
                                           uint64_t command_us,
                                           uint64_t sensor_us,
                                           uint64_t comms_us)
{
    if (ctrl == NULL) return;
    steering_safety_set_timeouts(&ctrl->safety, command_us, sensor_us, comms_us);
}

void steering_control_set_global_enabled(steering_control_t* ctrl, bool enabled)
{
    if (ctrl == NULL) return;
    steering_safety_set_global_enabled(&ctrl->safety, enabled);
}

void steering_control_set_local_switch(steering_control_t* ctrl, bool on)
{
    if (ctrl == NULL) return;
    steering_safety_set_local_switch(&ctrl->safety, on);
}

void steering_control_set_output_hal(steering_control_t* ctrl,
                                      steering_output_hal_t* hal)
{
    if (ctrl == NULL) return;
    /* steering_output already initialized, just update HAL ref */
    ctrl->output.hal = hal;
}

void steering_control_set_output_deadzone(steering_control_t* ctrl, float deadzone)
{
    if (ctrl == NULL) return;
    steering_output_set_deadzone(&ctrl->output, deadzone);
}

void steering_control_service_step(runtime_component_t* comp,
                                   uint64_t timestamp_us)
{
    /* Fallback for service_task invocation.
     * Constructs a synthetic fast_cycle_context and runs all three phases. */
    fast_cycle_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.timestamp_us = timestamp_us;
    ctx.period_us    = 10000;
    ctx.cycle_id     = 0;

    steering_control_fast_input(comp, &ctx);
    steering_control_fast_process(comp, &ctx);
    steering_control_fast_output(comp, &ctx);
}

const snapshot_buffer_t* steering_control_get_diag_snapshot(
    const steering_control_t* ctrl)
{
    if (ctrl == NULL) return NULL;
    return &ctrl->diag_snapshot;
}

const steering_safety_t* steering_control_get_safety(
    const steering_control_t* ctrl)
{
    if (ctrl == NULL) return NULL;
    return &ctrl->safety;
}

const steering_output_t* steering_control_get_output(
    const steering_control_t* ctrl)
{
    if (ctrl == NULL) return NULL;
    return &ctrl->output;
}
