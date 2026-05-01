/* was_sensor.c — Wheel Angle Sensor Implementation (STEER-MIG-001 AP-D)
 *
 * Reads raw ADC, linearly interpolates to degrees, evaluates freshness
 * and plausibility, publishes enhanced snapshot.
 *
 * Calibration: linear two-point mapping:
 *   degrees = cal_min_deg + (raw - cal_min_raw) * deg_span / raw_span
 *
 * Freshness: data is fresh for freshness_timeout_us after last read.
 * Plausibility: angle must be within ±angle_abs_max_deg.
 *
 * No Arduino, no heap, no HAL direct access.
 */

#include "was_sensor.h"
#include <string.h>
#include <math.h>

/* ---- Reason strings ---- */

static const char* s_reason_strings[] = {
    "NONE",
    "NO_ADC",
    "CAL_SPAN_ZERO",
    "RAW_OUT_OF_RANGE",
    "ANGLE_UNPLAUSIBLE",
    "STALE",
    "NOT_YET_READ"
};

/* ---- Internal: evaluate freshness ---- */

static bool is_fresh(uint64_t last_read_us, uint64_t now_us,
                     uint64_t timeout_us)
{
    if (!timeout_us) return false;
    if (now_us < last_read_us) return false;
    return (now_us - last_read_us) <= timeout_us;
}

/* ---- Internal: convert raw ADC to voltage ---- */

static float raw_to_voltage(uint16_t raw, float vref)
{
    return ((float)raw / 65535.0f) * vref;
}

/* ---- Service step implementation ---- */

static void was_sensor_service_step_fn(runtime_component_t* comp,
                                       uint64_t timestamp_us)
{
    was_sensor_t* was = (was_sensor_t*)comp;
    if (was == NULL) return;

    was_sensor_data_t sample;
    memset(&sample, 0, sizeof(sample));

    /* ---- Guard: no ADC bound ---- */
    if (was->adc == NULL) {
        sample.valid    = false;
        sample.fresh    = false;
        sample.reason   = WAS_REASON_NO_ADC;
        sample.degrees  = was->last_valid_degrees;  /* preserve last known */
        sample.raw      = 0;
        sample.voltage  = 0.0f;
        sample.timestamp_us = was->last_read_us;
        snapshot_buffer_set(&was->was_snapshot, &sample);
        was->current = sample;
        return;
    }

    /* ---- 1. Read raw ADC ---- */
    uint16_t raw = ads1118_get_raw(was->adc);
    was->last_read_us = timestamp_us;
    was->has_ever_read = true;

    sample.raw = raw;
    sample.voltage = raw_to_voltage(raw, was->vref);
    sample.timestamp_us = timestamp_us;

    /* ---- 2. Raw range check ---- */
    /* ADS1118 returns 0..65535 for a 16-bit signed conversion.
     * Values > 32767 indicate negative differential input.
     * For WAS, we expect positive single-ended readings 0..65535. */
    if (raw == 0 && was->cal_min_raw > 0) {
        /* Short to ground — invalid */
        sample.valid  = false;
        sample.reason = WAS_REASON_RAW_OUT_OF_RANGE;
        sample.degrees = was->last_valid_degrees;
        sample.fresh  = true;  /* ADC is responding, data is just bad */
        snapshot_buffer_set(&was->was_snapshot, &sample);
        was->current = sample;
        return;
    }

    /* ---- 3. Linear interpolation to degrees ---- */
    uint16_t raw_span = was->cal_max_raw - was->cal_min_raw;
    if (raw_span == 0u) {
        sample.valid  = false;
        sample.reason = WAS_REASON_CAL_SPAN_ZERO;
        sample.degrees = 0.0f;
        sample.fresh  = true;
        snapshot_buffer_set(&was->was_snapshot, &sample);
        was->current = sample;
        return;
    }

    float deg_span = was->cal_max_deg - was->cal_min_deg;
    float degrees = was->cal_min_deg
                  + ((float)(raw - was->cal_min_raw) * deg_span)
                    / (float)raw_span;

    sample.degrees = degrees;

    /* ---- 4. Plausibility check ---- */
    if (fabsf(degrees) > was->angle_abs_max_deg) {
        sample.valid  = false;
        sample.reason = WAS_REASON_ANGLE_UNPLAUSIBLE;
        sample.degrees = was->last_valid_degrees;  /* preserve last known */
        sample.fresh  = true;
        snapshot_buffer_set(&was->was_snapshot, &sample);
        was->current = sample;
        return;
    }

    /* ---- 5. Jump rate check (if configured) ---- */
    if (was->max_jump_rate_deg > 0.0f && was->has_ever_read) {
        float jump = fabsf(degrees - was->last_valid_degrees);
        if (jump > was->max_jump_rate_deg) {
            /* Sudden jump — could be ADC glitch, preserve last known */
            sample.valid  = false;
            sample.reason = WAS_REASON_ANGLE_UNPLAUSIBLE;
            sample.degrees = was->last_valid_degrees;
            sample.fresh  = true;
            snapshot_buffer_set(&was->was_snapshot, &sample);
            was->current = sample;
            return;
        }
    }

    /* ---- 6. Valid reading ---- */
    sample.valid  = true;
    sample.reason = WAS_REASON_NONE;
    was->last_valid_degrees = degrees;

    /* ---- 7. Freshness evaluation ---- */
    sample.fresh = true;  /* just read, so it's fresh */

    /* ---- 8. Publish snapshot ---- */
    snapshot_buffer_set(&was->was_snapshot, &sample);
    was->current = sample;
}

/* ---- Public API ---- */

void was_sensor_init(was_sensor_t* was)
{
    if (was == NULL) return;

    memset(was, 0, sizeof(was_sensor_t));

    /* Conservative defaults per STEER-MIG-001 spec */
    was->cal_min_raw        = 0;
    was->cal_max_raw        = 65535;
    was->cal_min_deg        = -40.0f;
    was->cal_max_deg        = 40.0f;
    was->vref               = 3.3f;
    was->angle_abs_max_deg  = 40.0f;
    was->max_jump_rate_deg  = 5.0f;      /* max 5° per 10ms cycle */
    was->freshness_timeout_us = 200000;   /* 200 ms */

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

void was_sensor_set_vref(was_sensor_t* was, float vref)
{
    if (was == NULL) return;
    if (vref > 0.0f) was->vref = vref;
}

void was_sensor_set_plausibility(was_sensor_t* was,
                                  float abs_max_deg,
                                  float max_jump_rate_deg)
{
    if (was == NULL) return;
    was->angle_abs_max_deg = abs_max_deg;
    was->max_jump_rate_deg = max_jump_rate_deg;
}

void was_sensor_set_freshness_timeout(was_sensor_t* was, uint64_t timeout_us)
{
    if (was == NULL) return;
    was->freshness_timeout_us = timeout_us;
}

void was_sensor_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    was_sensor_service_step_fn(comp, timestamp_us);
}

const snapshot_buffer_t* was_sensor_get_snapshot(const was_sensor_t* was)
{
    if (was == NULL) return NULL;
    return &was->was_snapshot;
}

const char* was_sensor_reason_str(was_reason_t reason)
{
    if (reason > WAS_REASON_NOT_YET_READ) return "UNKNOWN";
    return s_reason_strings[reason];
}
