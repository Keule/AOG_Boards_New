#include "runtime_component.h"

#define RUNTIME_COMPONENT_MAX 32

static runtime_component_t* s_components[RUNTIME_COMPONENT_MAX];
static size_t s_component_count = 0;

int runtime_component_register(runtime_component_t* component)
{
    if (component == NULL) {
        return -1;
    }

    if (s_component_count >= RUNTIME_COMPONENT_MAX) {
        return -2;
    }

    s_components[s_component_count] = component;
    s_component_count++;

    return 0;
}

size_t runtime_component_count(void)
{
    return s_component_count;
}

runtime_component_t* runtime_component_get(size_t index)
{
    if (index >= s_component_count) {
        return NULL;
    }

    return s_components[index];
}

void runtime_service_step_all(uint64_t timestamp_us)
{
    for (size_t i = 0; i < s_component_count; i++) {
        runtime_component_t* comp = s_components[i];
        if (comp != NULL && comp->service_step != NULL) {
            comp->service_step(comp, timestamp_us);
        }
    }
}

void runtime_service_step_group(service_group_t group, uint64_t timestamp_us)
{
    for (size_t i = 0; i < s_component_count; i++) {
        runtime_component_t* comp = s_components[i];
        if (comp != NULL
            && comp->service_step != NULL
            && comp->service_group == group) {
            comp->service_step(comp, timestamp_us);
        }
    }
}

size_t runtime_component_count_group(service_group_t group)
{
    size_t count = 0;
    for (size_t i = 0; i < s_component_count; i++) {
        if (s_components[i] != NULL && s_components[i]->service_group == group) {
            count++;
        }
    }
    return count;
}
