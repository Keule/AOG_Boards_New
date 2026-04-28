#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "runtime_component.h"

#ifdef __cplusplus
extern "C" {
#endif

void runtime_init(void);
bool runtime_register(runtime_component_t* component);
void runtime_start(void);
void runtime_step_once(void);
size_t runtime_registered_count(void);

#ifdef __cplusplus
}
#endif
