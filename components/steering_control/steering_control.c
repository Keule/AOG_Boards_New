#include "steering_control.h"

#include <string.h>

void steering_control_init(steering_control_t* ctrl, was_sensor_t* was, imu_bno085_t* imu)
{
    if (ctrl == NULL) {
        return;
    }

    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->component.name = "steering_control";
    ctrl->component.user_data = ctrl;
    ctrl->component.service_step = steering_control_service_step;
    ctrl->was = was;
    ctrl->imu = imu;
}

void steering_control_set_input(steering_control_t* ctrl, const steering_input_t* input)
{
    if (ctrl == NULL || input == NULL) {
        return;
    }

    ctrl->input = *input;
}

const steering_command_t* steering_control_get_command(const steering_control_t* ctrl)
{
    return (ctrl != NULL) ? &ctrl->command : NULL;
}

void steering_control_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    steering_control_t* ctrl;
    const was_sensor_sample_t* was;
    float error;

    if (comp == NULL || comp->user_data == NULL) {
        return;
    }

    ctrl = (steering_control_t*)comp->user_data;
    was = was_sensor_get_sample(ctrl->was);
    if (was == NULL || !was->valid || !ctrl->input.valid) {
        return;
    }

    error = ctrl->input.target_angle_deg - was->angle_deg;

    ctrl->command.valid = true;
    ctrl->command.dir_right = (error >= 0.0f);
    ctrl->command.pwm = (error >= 0.0f) ? error : -error;
    if (ctrl->command.pwm > 100.0f) {
        ctrl->command.pwm = 100.0f;
    }
    ctrl->command.timestamp_us = timestamp_us;
}
