#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "runtime_component.h"
#include "ads1118.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    int16_t raw;
    float angle_deg;
    uint64_t timestamp_us;
} was_sensor_sample_t;

typedef struct {
    runtime_component_t component;
    ads1118_t* adc;
    int16_t calib_min;
    int16_t calib_max;
    was_sensor_sample_t sample;
} was_sensor_t;

void was_sensor_init(was_sensor_t* was, ads1118_t* adc, int16_t calib_min, int16_t calib_max);
const was_sensor_sample_t* was_sensor_get_sample(const was_sensor_t* was);
void was_sensor_service_step(runtime_component_t* comp, uint64_t timestamp_us);

#ifdef __cplusplus
}
#endif
