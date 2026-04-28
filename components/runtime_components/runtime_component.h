#pragma once

#include <stddef.h>

#include "runtime_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtime_component runtime_component_t;

typedef void (*runtime_component_fast_fn_t)(
    runtime_component_t* component,
    const fast_cycle_context_t* ctx
);

struct runtime_component {
    const char* name;
    void* user_data;
    runtime_component_fast_fn_t fast_input;
    runtime_component_fast_fn_t fast_process;
    runtime_component_fast_fn_t fast_output;
};

int runtime_component_register(runtime_component_t* component);
size_t runtime_component_count(void);
runtime_component_t* runtime_component_get(size_t index);

#ifdef __cplusplus
}
#endif
