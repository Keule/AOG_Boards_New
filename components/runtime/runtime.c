#include "runtime.h"

#define RUNTIME_MAX_COMPONENTS 16U

static runtime_component_t* s_components[RUNTIME_MAX_COMPONENTS];
static size_t s_count = 0;
static bool s_started = false;

void runtime_init(void)
{
    size_t i = 0;
    for (i = 0; i < RUNTIME_MAX_COMPONENTS; ++i) {
        s_components[i] = 0;
    }
    s_count = 0;
    s_started = false;
}

bool runtime_register(runtime_component_t* component)
{
    if (component == 0 || s_count >= RUNTIME_MAX_COMPONENTS) {
        return false;
    }

    s_components[s_count++] = component;
    return true;
}

void runtime_start(void)
{
    s_started = true;
}

void runtime_step_once(void)
{
    size_t i = 0;
    if (!s_started) {
        return;
    }

    for (i = 0; i < s_count; ++i) {
        runtime_component_step(s_components[i]);
    }
}

size_t runtime_registered_count(void)
{
    return s_count;
}
