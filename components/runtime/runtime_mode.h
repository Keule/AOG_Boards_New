#pragma once

#include "runtime_types.h"

/* ---- Host-testable mode/profile logic (no ESP-IDF dependency) ----
 *
 * This header exposes the pure-logic parts of runtime mode switching
 * so they can be unit-tested in native builds without FreeRTOS/esp_timer.
 *
 * On ESP targets, runtime.c calls these functions internally.
 * On host targets, test code calls them directly.                     */

/* Initialise mode subsystem.  Sets mode to WORK and applies work profiles. */
void runtime_mode_init(void);

/* Set the system mode.  Returns 0 on success, -1 on invalid mode.
 * Applies the corresponding profile set to s_profiles[]. */
int runtime_mode_set(system_mode_t mode);

/* Get the current system mode. */
system_mode_t runtime_mode_get(void);

/* Get the service profile for a group.
 * Returns NULL for invalid group index. */
const service_profile_t* runtime_mode_get_profile(service_group_t group);

/* Get the work-mode profile for a specific group (const reference). */
const service_profile_t* runtime_mode_work_profile(service_group_t group);

/* Get the config-mode profile for a specific group (const reference). */
const service_profile_t* runtime_mode_config_profile(service_group_t group);
