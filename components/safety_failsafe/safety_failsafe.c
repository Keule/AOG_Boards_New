#include "safety_failsafe.h"

#include <string.h>

void safety_failsafe_init(safety_failsafe_t* safety, steering_control_t* control, uint32_t watchdog_timeout_ms)
{
    if (safety == NULL) {
        return;
    }

    memset(safety, 0, sizeof(*safety));
    safety->component.name = "safety_failsafe";
    safety->component.user_data = safety;
    safety->component.service_step = safety_failsafe_service_step;
    safety->control = control;
    safety->watchdog_timeout_ms = watchdog_timeout_ms;
    safety->failsafe_active = true;
    safety->high_z_outputs = true;
}

bool safety_failsafe_is_active(const safety_failsafe_t* safety)
{
    return (safety != NULL) ? safety->failsafe_active : true;
}

bool safety_failsafe_outputs_high_z(const safety_failsafe_t* safety)
{
    return (safety != NULL) ? safety->high_z_outputs : true;
}

void safety_failsafe_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    safety_failsafe_t* safety;
    const steering_command_t* cmd;
    uint64_t timeout_us;

    if (comp == NULL || comp->user_data == NULL) {
        return;
    }

    safety = (safety_failsafe_t*)comp->user_data;
    cmd = steering_control_get_command(safety->control);
    timeout_us = ((uint64_t)safety->watchdog_timeout_ms) * 1000ULL;

    if (cmd == NULL || !cmd->valid || timestamp_us < cmd->timestamp_us || (timestamp_us - cmd->timestamp_us) > timeout_us) {
        safety->failsafe_active = true;
        safety->high_z_outputs = true;
    } else {
        safety->failsafe_active = false;
        safety->high_z_outputs = false;
    }
}
