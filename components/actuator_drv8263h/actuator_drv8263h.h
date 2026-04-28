#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "runtime_component.h"
#include "steering_control.h"
#include "safety_failsafe.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    runtime_component_t component;
    steering_control_t* control;
    safety_failsafe_t* safety;
    uint8_t pwm_pin;
    uint8_t dir_pin;
    uint8_t failsafe_pin;
    bool simulated;
} actuator_drv8263h_t;

void actuator_drv8263h_init(actuator_drv8263h_t* act, steering_control_t* control, safety_failsafe_t* safety, uint8_t pwm_pin, uint8_t dir_pin, uint8_t failsafe_pin, bool simulated);
void actuator_drv8263h_service_step(runtime_component_t* comp, uint64_t timestamp_us);

#ifdef __cplusplus
}
#endif
