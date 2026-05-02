#pragma once
/*
 * Stub runtime_mode.h for native tests.
 * Provides declarations for runtime mode functions.
 */
#include "runtime_types.h"

void runtime_mode_init(void);
int runtime_mode_set(system_mode_t mode);
system_mode_t runtime_mode_get(void);
const service_profile_t* runtime_mode_get_profile(service_group_t group);
const service_profile_t* runtime_mode_work_profile(service_group_t group);
const service_profile_t* runtime_mode_config_profile(service_group_t group);
