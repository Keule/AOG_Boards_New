#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtime_component runtime_component_t;
typedef void (*runtime_component_step_fn)(runtime_component_t* component);

struct runtime_component {
    const char* name;
    void* user_data;
    runtime_component_step_fn step;
};

void runtime_component_step(runtime_component_t* component);

#ifdef __cplusplus
}
#endif
