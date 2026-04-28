/* was_sensor.c — Wheel Angle Sensor service-step implementation.
 *
 * Reads raw ADC, linearly interpolates to degrees, publishes snapshot.
 */

#include "was_sensor.h"
#include <string.h>

static void was_sensor_service_step_fn(runtime_component_t* comp,
                                       uint64_t timestamp_us)
{
    (void)timestamp_us;
    was_sensor_t* was = (was_sensor_t*)comp;
    if (was == NULL) {
        return;
    }

    /* Guard: no ADC bound yet. */
    if (was->adc == NULL) {
        was->data_valid = false;
        return;
    }

    /* 1. Read raw ADC value. */
    was->raw = ads1118_get_raw(was->adc);

    /* 2. Linear interpolation. */
    uint16_t raw_span = was->cal_max_raw - was->cal_min_raw;
    if (raw_span == 0u) {
        was->degrees   = 0.0f;
        was->data_valid = false;
        return;
    }

    float deg_span = was->cal_max_deg - was->cal_min_deg;
    was->degrees = was->cal_min_deg
                 + ((float)(was->raw - was->cal_min_raw) * deg_span)
                   / (float)raw_span;

    was->data_valid = true;

    /* 3. Publish into snapshot. */
    was_sensor_data_t sample;
    sample.raw     = was->raw;
    sample.degrees = was->degrees;
    sample.valid   = was->data_valid;

    snapshot_buffer_set(&was->was_snapshot, &sample);
}

void was_sensor_init(was_sensor_t* was)
{
    if (was == NULL) {
        return;
    }

    memset(was, 0, sizeof(was_sensor_t));

    was->cal_min_deg = -40.0f;
    was->cal_max_deg =  40.0f;

    snapshot_buffer_init(&was->was_snapshot, &was->was_storage,
                         sizeof(was_sensor_data_t));

    was->component.service_step = was_sensor_service_step_fn;
}

void was_sensor_set_adc(was_sensor_t* was, ads1118_t* adc)
{
    if (was == NULL) return;
    was->adc = adc;
}

void was_sensor_set_calibration(was_sensor_t* was,
                                uint16_t min_raw, uint16_t max_raw,
                                float min_deg, float max_deg)
{
    if (was == NULL) return;
    was->cal_min_raw = min_raw;
    was->cal_max_raw = max_raw;
    was->cal_min_deg = min_deg;
    was->cal_max_deg = max_deg;
}

void was_sensor_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    was_sensor_service_step_fn(comp, timestamp_us);
}

const snapshot_buffer_t* was_sensor_get_snapshot(const was_sensor_t* was)
{
    if (was == NULL) {
        return NULL;
    }
    return &was->was_snapshot;
}
