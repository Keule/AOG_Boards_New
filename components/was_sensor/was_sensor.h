#pragma once

/* was_sensor.h — Wheel Angle Sensor (WAS) Component (STEER-MIG-001 AP-D)
 *
 * Reads raw ADC (via ADS1118), applies linear calibration, evaluates
 * freshness and plausibility, publishes snapshot with full status.
 *
 * Data flow:
 *   raw ADC / voltage
 *     → normalized value
 *     → Lenkwinkel in degrees
 *     → Plausibilitaetsstatus
 *     → Timestamp / Freshness
 *
 * Plausibility checks:
 *   - Raw value outside ADC range (0..65535 for 16-bit)
 *   - Angle outside physical range (-40° to +40° configurable)
 *   - Sudden jumps > max_rate per cycle (configurable)
 *
 * Freshness:
 *   - Data is fresh for configured timeout after last successful read
 *   - After timeout, fresh=false but last valid angle is preserved
 *
 * No Arduino, no heap, no HAL direct access (via ads1118_t abstraction).
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

/* ---- Error/reason codes for sensor status ---- */

typedef enum {
    WAS_REASON_NONE             = 0,   /* All good */
    WAS_REASON_NO_ADC           = 1,   /* No ADC bound */
    WAS_REASON_CAL_SPAN_ZERO    = 2,   /* Calibration min==max raw */
    WAS_REASON_RAW_OUT_OF_RANGE = 3,   /* Raw ADC outside 0..65535 */
    WAS_REASON_ANGLE_UNPLAUSIBLE = 4,  /* Angle outside physical range */
    WAS_REASON_STALE            = 5,   /* Data expired (freshness timeout) */
    WAS_REASON_NOT_YET_READ     = 6    /* No reading yet */
} was_reason_t;

/* ---- Enhanced snapshot payload ---- */

typedef struct {
    uint16_t raw;           /**< ADC raw reading (0..65535) */
    float voltage;          /**< Approximate voltage (raw / 65535 * VREF) */
    float degrees;          /**< Calibrated angle in degrees */
    bool valid;             /**< True when conversion succeeded */
    bool fresh;             /**< True when within freshness timeout */
    uint64_t timestamp_us;  /**< Timestamp of last successful read */
    was_reason_t reason;    /**< Status/reason code */
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
    float vref;                       /**< ADC reference voltage (default 3.3V) */

    /* Plausibility limits */
    float angle_abs_max_deg;          /**< Absolute max angle for plausibility */
    float max_jump_rate_deg;          /**< Max allowed jump per cycle (deg) */

    /* Freshness timeout */
    uint64_t freshness_timeout_us;    /**< Freshness timeout in microseconds */

    /* Internal state */
    float last_valid_degrees;         /**< Last known good angle */
    bool has_ever_read;               /**< True after first successful read */
    uint64_t last_read_us;            /**< Timestamp of last service_step */

    /* Latest output */
    was_sensor_data_t current;

    /* Snapshot publication */
    snapshot_buffer_t was_snapshot;
    was_sensor_data_t was_storage;
} was_sensor_t;

/* ---- API ---- */

/** Initialize the component with conservative defaults.
 *  Default calibration: -40° to +40° over full ADC range.
 *  Default freshness timeout: 200ms.
 *  Default physical range: ±40°.
 *  Angle range: ±22.5° (STEER-MIG-001 spec). */
void was_sensor_init(was_sensor_t* was);

/** Bind an external ADS1118 driver instance (NOT owned). */
void was_sensor_set_adc(was_sensor_t* was, ads1118_t* adc);

/** Configure the two-point linear calibration. */
void was_sensor_set_calibration(was_sensor_t* was,
                                uint16_t min_raw, uint16_t max_raw,
                                float min_deg, float max_deg);

/** Set ADC reference voltage (for voltage output). Default: 3.3V. */
void was_sensor_set_vref(was_sensor_t* was, float vref);

/** Set plausibility limits. */
void was_sensor_set_plausibility(was_sensor_t* was,
                                  float abs_max_deg,
                                  float max_jump_rate_deg);

/** Set freshness timeout. Default: 200000 us (200 ms). */
void was_sensor_set_freshness_timeout(was_sensor_t* was, uint64_t timeout_us);

/** Service-step callback (also callable directly).
 *  Reads ADC, applies calibration, checks plausibility,
 *  evaluates freshness, publishes snapshot. */
void was_sensor_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/** Return a read-only pointer to the published snapshot. */
const snapshot_buffer_t* was_sensor_get_snapshot(const was_sensor_t* was);

/** Get reason code as human-readable string. */
const char* was_sensor_reason_str(was_reason_t reason);

#ifdef __cplusplus
}
#endif
