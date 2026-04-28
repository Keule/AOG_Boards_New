#include "runtime_component.h"

#define RUNTIME_COMPONENT_MAX 32

static runtime_component_t* s_components[RUNTIME_COMPONENT_MAX];
static size_t s_component_count = 0;

void runtime_component_clear_all(void)
{
    size_t i;
    for (i = 0; i < RUNTIME_COMPONENT_MAX; ++i) {
        s_components[i] = NULL;
    }
    s_component_count = 0;
}

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
