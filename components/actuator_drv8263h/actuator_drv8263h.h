#pragma once

/* actuator_drv8263h.h — DRV8263H PWM/DIR motor driver component (skeleton).
 *
 * Reads a steering_command_t snapshot from steering_control and translates
 * it to a simulated duty-cycle value.
 *
 * Consumes safety status snapshot from safety_failsafe. When safety is
 * triggered, output is functionally disabled (duty clamped to 0, simulated
 * high-Z / neutral state).
 *
 * Enable Policy:
 *   The actuator starts DISABLED. It auto-enables when it receives the first
 *   valid steering command with sensors_valid == true and safety not blocked.
 *   It auto-disables when:
 *     - No valid command is received for ACTUATOR_DISABLE_TIMEOUT cycles, OR
 *     - Safety becomes triggered, OR
 *     - Manual disable() is called.
 *   This ensures the actuator only drives when the full steering pipeline
 *   (AOG input + WAS + IMU + safety) is confirmed operational.
 *
 * No actual HAL GPIO / PWM calls in this skeleton. All output is abstract.
 *
 * This component does NOT:
 *   - call HAL GPIO/PWM functions (skeleton only)
 *   - decide about safety independently — it consumes the safety snapshot
 *   - call transport functions
 *   - use heap allocation
 *
 * No Arduino, no heap, no HAL, no transport.
 * Depends on: runtime_component.h, snapshot_buffer.h
 */

#include <stdint.h>
#include <stdbool.h>

#include "runtime_component.h"
#include "snapshot_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Safety status layout (must match safety_failsafe.h).
 * Duplicated here to avoid pulling in safety_failsafe.h as a dependency
 * (avoids circular CMake REQUIRES). Must stay in sync. */
typedef struct {
    bool triggered;        /**< True when failsafe/warning is active */
    uint32_t timeout_count;/**< Total number of timeout events */
    uint8_t flags;         /**< Reserved */
} actuator_safety_status_t;

/* ---- Auto-disable timeout ---- */

#define ACTUATOR_DISABLE_TIMEOUT  50  /* cycles without valid command before auto-disable */

/* ---- Main component struct ---- */

typedef struct {
    runtime_component_t component;    /**< MUST be first field */

    /* Input sources (NOT owned, set by caller) */
    const snapshot_buffer_t* command_source;   /**< steering_command_t snapshot */
    const snapshot_buffer_t* safety_source;    /**< safety_status_t snapshot */

    /* Abstract pin assignment (set at init, not used in skeleton) */
    uint8_t pwm_pin;
    uint8_t dir_pin;

    /* Simulated state */
    float current_duty;      /**< -1.0 … +1.0 (simulated) */
    bool enabled;            /**< Driver enable flag (auto-managed) */
    bool safety_blocked;     /**< True when safety failsafe is active */
    uint32_t update_count;   /**< Number of service-step updates executed */
    uint32_t no_valid_count; /**< Consecutive cycles without valid command */
} actuator_drv8263h_t;

/* ---- API ---- */

/** Initialise the driver with abstract pin assignments.
 *  Driver starts DISABLED and duty = 0. See Enable Policy above. */
void actuator_drv8263h_init(actuator_drv8263h_t* act,
                            uint8_t pwm_pin,
                            uint8_t dir_pin);

/** Bind the steering command snapshot source (NOT owned). */
void actuator_drv8263h_set_command_source(actuator_drv8263h_t* act,
                                          const snapshot_buffer_t* src);

/** Bind the safety status snapshot source (NOT owned).
 *  When safety is triggered, the actuator functionally disables output. */
void actuator_drv8263h_set_safety_source(actuator_drv8263h_t* act,
                                         const snapshot_buffer_t* src);

/** Service-step callback (also callable directly). */
void actuator_drv8263h_service_step(runtime_component_t* comp,
                                    uint64_t timestamp_us);

/** Return the current simulated duty cycle (-1.0 … +1.0). */
float actuator_drv8263h_get_duty(const actuator_drv8263h_t* act);

/** Return true when safety is currently blocking output. */
bool actuator_drv8263h_is_safety_blocked(const actuator_drv8263h_t* act);

/** Return true when the actuator is enabled (auto or manual). */
bool actuator_drv8263h_is_enabled(const actuator_drv8263h_t* act);

/** Manually enable the driver (overrides auto-enable policy). */
void actuator_drv8263h_enable(actuator_drv8263h_t* act);

/** Manually disable the driver (duty → 0.0, independent of safety). */
void actuator_drv8263h_disable(actuator_drv8263h_t* act);

#ifdef __cplusplus
}
#endif
