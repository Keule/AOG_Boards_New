/* safety_failsafe.c — Safety watchdog + failsafe component.
 *
 * Skeleton implementation:
 *   - On init: configure failsafe GPIO as output, drive to safe (inactive) level.
 *   - On service_step:
 *       1. Check if timestamp_us - last_feed_us > timeout.
 *       2. Update failsafe_triggered state.
 *       3. Publish safety_status_t to snapshot (PRIMARY interface).
 *       4. Drive failsafe GPIO as secondary hardware effect.
 */

#include "safety_failsafe.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/** Compute the "safe" (inactive) GPIO level — opposite of active level. */
static hal_gpio_level_t safe_level(hal_gpio_level_t active)
{
    return (active == HAL_GPIO_HIGH) ? HAL_GPIO_LOW : HAL_GPIO_HIGH;
}

static void safety_failsafe_service_step_fn(runtime_component_t* comp,
                                            uint64_t timestamp_us)
{
    safety_failsafe_t* sf = (safety_failsafe_t*)comp;
    if (sf == NULL) {
        return;
    }

    /* Check watchdog timeout. */
    if (sf->watchdog_timeout_us == 0u) {
        /* Timeout not configured — consider safe. */
        sf->failsafe_triggered = false;

        safety_status_t status;
        status.triggered     = false;
        status.timeout_count = sf->timeout_count;
        status.flags         = 0;
        snapshot_buffer_set(&sf->safety_snapshot, &status);
        return;
    }

    uint64_t elapsed = timestamp_us - sf->last_feed_us;
    if (elapsed > sf->watchdog_timeout_us) {
        /* ---- Timeout detected: trigger failsafe ---- */
        if (!sf->failsafe_triggered) {
            sf->failsafe_triggered = true;
            sf->timeout_count++;
        }

        /* Drive failsafe GPIO to active level (secondary hardware effect). */
        hal_gpio_set(sf->failsafe_pin, sf->failsafe_active_level);
    } else if (sf->failsafe_triggered) {
        /* Watchdog was re-fed in time — clear failsafe. */
        sf->failsafe_triggered = false;

        /* Drive failsafe GPIO to safe (inactive) level. */
        hal_gpio_set(sf->failsafe_pin, safe_level(sf->failsafe_active_level));
    }

    /* ---- Publish safety status snapshot (PRIMARY interface) ---- */
    safety_status_t status;
    status.triggered     = sf->failsafe_triggered;
    status.timeout_count = sf->timeout_count;
    status.flags         = 0;
    snapshot_buffer_set(&sf->safety_snapshot, &status);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

void safety_failsafe_init(safety_failsafe_t* sf,
                          uint8_t failsafe_pin,
                          hal_gpio_level_t active_level)
{
    if (sf == NULL) {
        return;
    }

    memset(sf, 0, sizeof(safety_failsafe_t));

    sf->component.name         = "safety_failsafe";
    sf->component.user_data    = sf;
    sf->component.fast_input   = NULL;
    sf->component.fast_process = NULL;
    sf->component.fast_output  = NULL;
    sf->component.service_step = safety_failsafe_service_step_fn;

    sf->failsafe_pin          = failsafe_pin;
    sf->failsafe_active_level = active_level;
    sf->last_feed_us          = 0u;
    sf->watchdog_timeout_us   = 100000u;  /* default 100 ms */
    sf->failsafe_triggered    = false;
    sf->timeout_count         = 0u;

    /* Init safety status snapshot for downstream consumers. */
    snapshot_buffer_init(&sf->safety_snapshot,
                         &sf->safety_storage,
                         sizeof(safety_status_t));

    /* Configure failsafe GPIO as output, drive to safe (inactive) level. */
    hal_gpio_set_mode(sf->failsafe_pin, HAL_GPIO_MODE_OUTPUT);
    hal_gpio_set(sf->failsafe_pin, safe_level(active_level));

    /* Register with the runtime watchdog subsystem. */
    runtime_watchdog_register_task("safety_failsafe");
}

void safety_failsafe_set_timeout(safety_failsafe_t* sf,
                                 uint64_t timeout_us)
{
    if (sf == NULL) {
        return;
    }
    sf->watchdog_timeout_us = timeout_us;
}

void safety_failsafe_feed(safety_failsafe_t* sf,
                          uint64_t timestamp_us)
{
    if (sf == NULL) {
        return;
    }
    sf->last_feed_us = timestamp_us;
}

void safety_failsafe_service_step(runtime_component_t* comp,
                                  uint64_t timestamp_us)
{
    safety_failsafe_service_step_fn(comp, timestamp_us);
}

bool safety_failsafe_is_triggered(const safety_failsafe_t* sf)
{
    if (sf == NULL) {
        return true;  /* fail-safe: assume triggered if NULL */
    }
    return sf->failsafe_triggered;
}

const snapshot_buffer_t* safety_failsafe_get_snapshot(
    const safety_failsafe_t* sf)
{
    if (sf == NULL) {
        return NULL;
    }
    return &sf->safety_snapshot;
}
