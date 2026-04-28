#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "runtime_component.h"
#include "transport_udp.h"
#include "steering_control.h"
#include "safety_failsafe.h"
#include "aog_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AOG_PGN_STEERING_INPUT  300U
#define AOG_PGN_STEERING_STATUS 301U

typedef struct {
    runtime_component_t component;
    transport_udp_t* udp;
    steering_control_t* control;
    safety_failsafe_t* safety;
    aog_parser_t parser;
} aog_steering_app_t;

void aog_steering_app_init(aog_steering_app_t* app, transport_udp_t* udp, steering_control_t* control, safety_failsafe_t* safety);
void aog_steering_app_service_step(runtime_component_t* comp, uint64_t timestamp_us);

#ifdef __cplusplus
}
#endif
