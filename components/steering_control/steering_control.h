#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "runtime_component.h"
#include "was_sensor.h"
#include "imu_bno085.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    float target_angle_deg;
    uint64_t timestamp_us;
} steering_input_t;

typedef struct {
    bool valid;
    float pwm;
    bool dir_right;
    uint64_t timestamp_us;
} steering_command_t;

typedef struct {
    runtime_component_t component;
    was_sensor_t* was;
    imu_bno085_t* imu;
    steering_input_t input;
    steering_command_t command;
} steering_control_t;

void steering_control_init(steering_control_t* ctrl, was_sensor_t* was, imu_bno085_t* imu);
void steering_control_set_input(steering_control_t* ctrl, const steering_input_t* input);
const steering_command_t* steering_control_get_command(const steering_control_t* ctrl);
void steering_control_service_step(runtime_component_t* comp, uint64_t timestamp_us);

#ifdef __cplusplus
}
#endif
