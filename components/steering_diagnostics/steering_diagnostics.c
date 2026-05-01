/* steering_diagnostics.c — Steering Diagnostics Implementation (STEER-MIG-001)
 *
 * Collects diagnostics from steering_control, maintains error history,
 * publishes aggregated diagnostics snapshot.
 */

#include "steering_diagnostics.h"
#include <string.h>

/* ---- Internal: add error to history ring buffer ---- */

static void push_error(steering_diagnostics_t* state,
                       steer_safety_reason_t reason, uint64_t ts)
{
    uint8_t idx = state->error_history_index;
    state->error_history[idx].reason      = reason;
    state->error_history[idx].timestamp_us = ts;
    state->error_history_index = (idx + 1) % STEER_DIAG_HISTORY_SIZE;
    if (state->error_history_count < STEER_DIAG_HISTORY_SIZE) {
        state->error_history_count++;
    }
}

/* ---- Public API ---- */

void steering_diagnostics_init(steering_diagnostics_t_comp* diag)
{
    if (diag == NULL) return;

    memset(diag, 0, sizeof(steering_diagnostics_t_comp));

    snapshot_buffer_init(&diag->diag_snapshot, &diag->diag_storage,
                         sizeof(steering_diagnostics_t));
}

void steering_diagnostics_set_source(steering_diagnostics_t_comp* diag,
                                      const snapshot_buffer_t* source)
{
    if (diag == NULL) return;
    diag->diag_source = source;
}

void steering_diagnostics_service_step(steering_diagnostics_t_comp* diag,
                                        uint64_t timestamp_us)
{
    if (diag == NULL) return;

    steering_diagnostics_t* state = &diag->state;

    /* Track uptime */
    if (state->first_cycle_us == 0) {
        state->first_cycle_us = timestamp_us;
    }
    state->last_cycle_us = timestamp_us;

    /* Read from steering_control diagnostics snapshot */
    if (diag->diag_source != NULL) {
        steer_diag_snapshot_t ds;
        if (snapshot_buffer_get(diag->diag_source, &ds)) {
            /* Track safety transitions */
            bool was_ok = state->safety_ok;
            state->safety_ok = ds.safety_ok;
            state->safety_reason = ds.safety_reason;
            state->safety_block_count = ds.safety_block_count;
            state->cycle_count = ds.cycle_count;
            state->total_safety_blocks = ds.safety_block_count;

            /* Log safety violations to error history */
            if (was_ok && !ds.safety_ok) {
                push_error(state, ds.safety_reason, ds.timestamp_us);
            }

            /* Copy PID info */
            state->setpoint_deg = ds.setpoint_deg;
            state->actual_deg   = ds.actual_deg;
            state->error_deg    = ds.error_deg;
            state->pid_output   = ds.pid_output;
            state->integral     = ds.integral;
        }
    }

    /* Publish */
    snapshot_buffer_set(&diag->diag_snapshot, state);
}

const snapshot_buffer_t* steering_diagnostics_get_snapshot(
    const steering_diagnostics_t_comp* diag)
{
    if (diag == NULL) return NULL;
    return &diag->diag_snapshot;
}

const steer_error_entry_t* steering_diagnostics_get_error_history(
    const steering_diagnostics_t_comp* diag,
    uint8_t* count)
{
    if (diag == NULL) return NULL;
    if (count != NULL) {
        *count = diag->state.error_history_count;
    }
    return diag->state.error_history;
}
