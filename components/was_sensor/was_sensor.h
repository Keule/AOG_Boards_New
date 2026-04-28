#pragma once

/* was_sensor.h — Wheel Angle Sensor (ADS1118 ADC) component.
 *
 * Reads raw ADC from an ADS1118, applies linear calibration,
 * and publishes results into a snapshot_buffer_t.
 *
 * No Arduino, no heap, no HAL, no transport.
 * Depends on: runtime_component.h, snapshot_buffer.h, ads1118.h
 */

#include <stdint.h>
#include <stdbool.h>

#include "runtime_component.h"
#include "snapshot_buffer.h"
#include "ads1118.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Lightweight snapshot payload ---- */

typedef struct {
    uint16_t raw;       /**< ADC raw reading */
    float degrees;      /**< Calibrated angle in degrees */
    bool valid;         /**< True when conversion succeeded */
} was_sensor_data_t;

/* ---- Main component struct ---- */

typedef struct {
    runtime_component_t component;    /**< MUST be first field */

    ads1118_t* adc;                   /**< NOT owned, set by caller */

    /* Calibration endpoints */
    uint16_t cal_min_raw;             /**< ADC raw at min angle */
    uint16_t cal_max_raw;             /**< ADC raw at max angle */
    float cal_min_deg;                /**< Min angle in degrees */
    float cal_max_deg;                /**< Max angle in degrees */

    /* Latest output values */
    uint16_t raw;
    float degrees;
    bool data_valid;

    /* Snapshot publication */
    snapshot_buffer_t was_snapshot;
    was_sensor_data_t was_storage;
} was_sensor_t;

/* ---- API ---- */

/** Zero-initialise the component and set its service_step callback. */
void was_sensor_init(was_sensor_t* was);

/** Bind an external ADS1118 driver instance (NOT owned). */
void was_sensor_set_adc(was_sensor_t* was, ads1118_t* adc);

/** Configure the two-point linear calibration. */
void was_sensor_set_calibration(was_sensor_t* was,
                                uint16_t min_raw, uint16_t max_raw,
                                float min_deg, float max_deg);

/** Service-step callback (also callable directly). */
void was_sensor_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/** Return a read-only pointer to the published snapshot. */
const snapshot_buffer_t* was_sensor_get_snapshot(const was_sensor_t* was);

#ifdef __cplusplus
}
#endif
