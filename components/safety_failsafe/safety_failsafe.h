#pragma once

/* safety_failsafe.h — Safety watchdog + failsafe component.
 *
 * Monitors a software watchdog (feed by steering_control or service loop).
 * On timeout:
 *   1. Publishes failsafe status via snapshot (PRIMARY interface for actuator)
 *   2. Drives failsafe GPIO pin to active level (secondary hardware effect)
 *
 * This is the ONLY component that should be the source of safety status.
 * The actuator consumes the safety snapshot to functionally disable output.
 *
 * No Arduino, no heap, no transport.
 * Depends on: runtime_component.h, runtime_watchdog.h, hal_gpio.h,
 *             runtime_snapshot.h
 */

#include <stdint.h>
#include <stdbool.h>

#include "runtime_component.h"
#include "runtime_watchdog.h"
#include "hal_gpio.h"
#include "snapshot_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Safety status (snapshot payload) ---- */

typedef struct {
    bool triggered;        /**< True when failsafe/warning is active */
    uint32_t timeout_count;/**< Total number of timeout events */
    uint8_t flags;         /**< Reserved for future use */
} safety_status_t;

/* ---- Main component struct ---- */

typedef struct {
    runtime_component_t component;    /**< MUST be first field */

    /* Configuration (set during init) */
    uint8_t failsafe_pin;                      /**< GPIO for failsafe output */
    hal_gpio_level_t failsafe_active_level;    /**< Level when failsafe triggered */

    /* Watchdog state */
    uint64_t last_feed_us;             /**< Timestamp of last feed() call */
    uint64_t watchdog_timeout_us;      /**< Timeout threshold (e.g. 100 000 = 100 ms) */

    /* Runtime state */
    bool failsafe_triggered;           /**< True when failsafe is active */
    uint32_t timeout_count;            /**< Number of timeout events */

    /* Safety status snapshot for downstream consumers (actuator, etc.) */
    snapshot_buffer_t safety_snapshot;
    safety_status_t safety_storage;
} safety_failsafe_t;

/* ---- API ---- */

/** Initialise the failsafe component with pin and active level.
 *  Configures the GPIO as output and sets it to the safe (inactive) level.
 *  Registers with the runtime watchdog subsystem. */
void safety_failsafe_init(safety_failsafe_t* sf,
                          uint8_t failsafe_pin,
                          hal_gpio_level_t active_level);

/** Set the watchdog timeout in microseconds. */
void safety_failsafe_set_timeout(safety_failsafe_t* sf,
                                 uint64_t timeout_us);

/** Feed the watchdog — called by steering_control or the service loop. */
void safety_failsafe_feed(safety_failsafe_t* sf,
                          uint64_t timestamp_us);

/** Service-step callback (also callable directly).
 *  Checks watchdog timeout, updates state, publishes safety snapshot. */
void safety_failsafe_service_step(runtime_component_t* comp,
                                  uint64_t timestamp_us);

/** Return true when failsafe is currently triggered. */
bool safety_failsafe_is_triggered(const safety_failsafe_t* sf);

/** Get the safety status snapshot buffer (for wiring to actuator).
 *  Returns read-only pointer to the snapshot. */
const snapshot_buffer_t* safety_failsafe_get_snapshot(
    const safety_failsafe_t* sf);

#ifdef __cplusplus
}
#endif
