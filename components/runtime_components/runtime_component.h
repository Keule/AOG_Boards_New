#pragma once

#include <stddef.h>
#include <stdint.h>

#include "runtime_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtime_component runtime_component_t;

typedef void (*runtime_component_fast_fn_t)(
    runtime_component_t* component,
    const fast_cycle_context_t* ctx
);

/* Service step callback: called from the service task (NOT from task_fast).
 * timestamp_us: current timestamp in microseconds, provided by the runtime.
 * Components may perform buffered I/O, state machine steps, and
 * inter-component data exchange here. No strict real-time guarantees. */
typedef void (*runtime_component_service_fn_t)(
    runtime_component_t* component,
    uint64_t timestamp_us
);

struct runtime_component {
    const char* name;
    void* user_data;
    runtime_component_fast_fn_t fast_input;
    runtime_component_fast_fn_t fast_process;
    runtime_component_fast_fn_t fast_output;
    runtime_component_service_fn_t service_step;
    service_group_t service_group;    /* Core-0 service task group assignment */
};

int runtime_component_register(runtime_component_t* component);
size_t runtime_component_count(void);
runtime_component_t* runtime_component_get(size_t index);

/* Iterate over all registered components and call their service_step()
 * callback if non-NULL. Subsystem-local execution — no business logic here.
 * timestamp_us: current timestamp in microseconds. */
void runtime_service_step_all(uint64_t timestamp_us);

/* Iterate over components in a specific service group and call
 * their service_step() callback if non-NULL. */
void runtime_service_step_group(service_group_t group, uint64_t timestamp_us);

/* Get the number of components in a specific service group. */
size_t runtime_component_count_group(service_group_t group);

#ifdef __cplusplus
}
#endif
