#pragma once

/* steering_safety.h — Safety Gate for AOG Steering (STEER-MIG-001 AP-C)
 *
 * Pure-logic safety evaluator. No hardware, no transport, no heap.
 * Evaluates 10 mandatory safety conditions and produces a single
 * safety_ok / safety_reason result per cycle.
 *
 * Depends on: runtime_types.h (fast_cycle_context_t)
 *
 * Safety conditions (Pflichtfaelle):
 *   1. Steering global disabled           → Motor aus
 *   2. Lokaler Steering-Schalter AUS      → Motor aus
 *   3. AOG-Steer-Command stale            → Motor aus
 *   4. Lenkwinkelsensor stale             → Motor aus
 *   5. Lenkwinkelsensor unplausibel       → Motor aus
 *   6. Zielwinkel ausserhalb Bereich      → begrenzen oder Motor aus
 *   7. Istwinkel ausserhalb Bereich       → Motor aus
 *   8. Reglerausgabe ausserhalb Bereich   → saturieren
 *   9. Kommunikationsverlust              → Motor aus
 *  10. Interner Fehlerstatus             → Motor aus
 */

#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#include "runtime_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Safety Reason Codes ---- */

typedef enum {
    STEER_SAFETY_OK                  = 0,   /* All conditions pass */
    STEER_SAFETY_GLOBAL_DISABLED     = 1,   /* Global steering disabled */
    STEER_SAFETY_LOCAL_SWITCH_OFF    = 2,   /* Local steering switch off */
    STEER_SAFETY_COMMAND_STALE       = 3,   /* AOG command too old */
    STEER_SAFETY_COMMAND_INVALID     = 4,   /* No valid command available */
    STEER_SAFETY_SENSOR_STALE        = 5,   /* WAS sensor data too old */
    STEER_SAFETY_SENSOR_INVALID      = 6,   /* WAS sensor reports invalid */
    STEER_SAFETY_SENSOR_UNPLAUSIBLE  = 7,   /* WAS reading outside physical range */
    STEER_SAFETY_SETPOINT_OOR        = 8,   /* Command angle outside range */
    STEER_SAFETY_ACTUAL_OOR          = 9,   /* Actual angle outside range */
    STEER_SAFETY_COMMS_LOST          = 10,  /* Communication timeout */
    STEER_SAFETY_INTERNAL_FAULT      = 11,  /* Internal error flag set */
    STEER_SAFETY_NOT_ENABLED         = 12   /* Not yet auto-enabled */
} steer_safety_reason_t;

/* ---- Configuration (conservative defaults per spec) ---- */

#define STEER_ANGLE_MIN_DEG          (-22.5f)
#define STEER_ANGLE_MAX_DEG          (22.5f)
#define STEER_ANGLE_ABS_MAX_DEG      (40.0f)  /* Absolute max for out-of-range detection */
#define STEER_COMMAND_TIMEOUT_US     (200000ULL)  /* 200 ms default */
#define STEER_SENSOR_TIMEOUT_US      (200000ULL)  /* 200 ms default */
#define STEER_COMMS_TIMEOUT_US       (500000ULL)  /* 500 ms default */
#define STEER_PWM_MIN                (-1.0f)
#define STEER_PWM_MAX                (1.0f)

/* ---- Sensor input for safety evaluation ---- */

typedef struct {
    float angle_deg;             /**< Current actual angle in degrees */
    bool valid;                  /**< Sensor reports valid reading */
    bool fresh;                  /**< Data within freshness timeout */
    uint64_t timestamp_us;       /**< Timestamp of last sensor update */
} steer_sensor_input_t;

/* ---- Command input for safety evaluation ---- */

typedef struct {
    float setpoint_deg;          /**< Commanded angle from AOG */
    float speed_ms;              /**< Vehicle speed from AOG */
    uint8_t status;              /**< 0=inactive, 1=active */
    bool valid;                  /**< True when a valid command is available */
    bool sensors_valid;          /**< True when WAS+IMU both valid */
    uint64_t timestamp_us;       /**< Timestamp of last command update */
} steer_command_input_t;

/* ---- Safety gate result ---- */

typedef struct {
    bool safe;                           /**< true = motor output allowed */
    steer_safety_reason_t reason;        /**< Reason code (STEER_SAFETY_OK when safe) */
    bool setpoint_clamped;               /**< true if setpoint was clamped */
    float clamped_setpoint_deg;          /**< Setpoint after clamping */
    bool pwm_saturated;                  /**< true if PWM was saturated */
    float saturated_pwm;                 /**< PWM after saturation */
} steer_safety_result_t;

/* ---- Main safety gate struct ---- */

typedef struct {
    /* Configuration */
    float angle_min_deg;          /**< Minimum allowed steering angle */
    float angle_max_deg;          /**< Maximum allowed steering angle */
    float angle_abs_max_deg;      /**< Absolute max for OOR detection */
    uint64_t command_timeout_us;  /**< Command freshness timeout (us) */
    uint64_t sensor_timeout_us;   /**< Sensor freshness timeout (us) */
    uint64_t comms_timeout_us;    /**< Communication timeout (us) */
    float pwm_min;                /**< Minimum PWM output */
    float pwm_max;                /**< Maximum PWM output */

    /* Runtime state */
    bool global_enabled;          /**< Global steering enable */
    bool local_switch;            /**< Local steering switch (true=ON) */
    bool internal_fault;          /**< Internal fault flag */
    uint64_t last_command_us;     /**< Timestamp of last received command */
    uint64_t last_comms_us;       /**< Timestamp of last communication activity */
    bool comms_active;            /**< Communication is active */

    /* Last evaluation result */
    steer_safety_result_t result;

    /* Statistics */
    uint32_t eval_count;
    uint32_t unsafe_count;
    uint32_t reason_counts[13];   /* Count per reason code */
} steering_safety_t;

/* ---- API ---- */

/** Initialize safety gate with conservative defaults.
 *  Default state: global_enabled=false, local_switch=false, motor OFF. */
void steering_safety_init(steering_safety_t* sg);

/** Configure angle limits. */
void steering_safety_set_angle_limits(steering_safety_t* sg,
                                       float min_deg, float max_deg,
                                       float abs_max_deg);

/** Configure timeouts. */
void steering_safety_set_timeouts(steering_safety_t* sg,
                                   uint64_t command_us,
                                   uint64_t sensor_us,
                                   uint64_t comms_us);

/** Configure PWM limits. */
void steering_safety_set_pwm_limits(steering_safety_t* sg,
                                     float min, float max);

/** Set global steering enable. false = motor OFF regardless of other conditions. */
void steering_safety_set_global_enabled(steering_safety_t* sg, bool enabled);

/** Set local steering switch. false = motor OFF. */
void steering_safety_set_local_switch(steering_safety_t* sg, bool on);

/** Set internal fault flag. true = motor OFF. */
void steering_safety_set_internal_fault(steering_safety_t* sg, bool fault);

/** Feed communication timestamp (called when AOG frames received). */
void steering_safety_feed_comms(steering_safety_t* sg, uint64_t timestamp_us);

/** Clear internal fault. */
void steering_safety_clear_fault(steering_safety_t* sg);

/** Evaluate all safety conditions.
 *  Returns the safety result. Must be called once per 100-Hz cycle.
 *  This is the FAST_PROCESS hook — pure logic, no I/O. */
steer_safety_result_t steering_safety_evaluate(
    steering_safety_t* sg,
    const steer_command_input_t* cmd,
    const steer_sensor_input_t* sensor,
    uint64_t now_us);

/** Get the last evaluation result. */
const steer_safety_result_t* steering_safety_get_result(
    const steering_safety_t* sg);

/** Get reason code as human-readable string. */
const char* steering_safety_reason_str(steer_safety_reason_t reason);

#ifdef __cplusplus
}
#endif
