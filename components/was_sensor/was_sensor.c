#include "was_sensor.h"

#include <string.h>

void was_sensor_init(was_sensor_t* was, ads1118_t* adc, int16_t calib_min, int16_t calib_max)
{
    if (was == NULL) {
        return;
    }

    memset(was, 0, sizeof(*was));
    was->component.name = "was_sensor";
    was->component.user_data = was;
    was->component.service_step = was_sensor_service_step;
    was->adc = adc;
    was->calib_min = calib_min;
    was->calib_max = calib_max;
}

const was_sensor_sample_t* was_sensor_get_sample(const was_sensor_t* was)
{
    return (was != NULL) ? &was->sample : NULL;
}

void was_sensor_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    was_sensor_t* was;
    const ads1118_sample_t* adc_sample;
    int32_t range;
    int32_t centered;

    if (comp == NULL || comp->user_data == NULL) {
        return;
    }

    was = (was_sensor_t*)comp->user_data;
    adc_sample = ads1118_get_sample(was->adc);
    if (adc_sample == NULL || !adc_sample->valid) {
        return;
    }

    range = (int32_t)was->calib_max - (int32_t)was->calib_min;
    if (range == 0) {
        return;
    }

    centered = ((int32_t)adc_sample->raw - (int32_t)was->calib_min);

    was->sample.raw = adc_sample->raw;
    was->sample.angle_deg = ((float)centered / (float)range) * 90.0f - 45.0f;
    was->sample.timestamp_us = timestamp_us;
    was->sample.valid = true;
}
