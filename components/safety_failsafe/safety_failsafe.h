#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "runtime_component.h"
#include "steering_control.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    runtime_component_t component;
    steering_control_t* control;
    uint32_t watchdog_timeout_ms;
    bool failsafe_active;
    bool high_z_outputs;
} safety_failsafe_t;

void safety_failsafe_init(safety_failsafe_t* safety, steering_control_t* control, uint32_t watchdog_timeout_ms);
bool safety_failsafe_is_active(const safety_failsafe_t* safety);
bool safety_failsafe_outputs_high_z(const safety_failsafe_t* safety);
void safety_failsafe_service_step(runtime_component_t* comp, uint64_t timestamp_us);

#ifdef __cplusplus
}
#endif
