#pragma once

#include "runtime_types.h"

/* Initialise runtime internals (mode defaults to WORK). */
void runtime_init(void);

/* Start task_fast (Core 1) and all Core-0 service tasks. */
void runtime_start(void);

/* Get the current service profile for a group.
 * Returns a pointer to live state — callers MUST NOT modify.
 * The pointer remains valid until the next runtime_set_system_mode() call. */
const service_profile_t* runtime_get_service_profile(service_group_t group);

/* Set the system mode (Work / Config).
 * Applies Work/Config service profiles to all four service groups and
 * propagates changes to running Core-0 service tasks.
 *
 * Returns 0 on success, -1 on invalid mode.
 *
 * Invalid modes (< 0 or >= SYSTEM_MODE_COUNT) are rejected.
 * Default mode at init is SYSTEM_MODE_WORK. */
int runtime_set_system_mode(system_mode_t mode);

/* Get the current system mode. */
system_mode_t runtime_get_system_mode(void);
