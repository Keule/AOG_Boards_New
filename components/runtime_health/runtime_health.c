#include "runtime_health.h"

static runtime_health_state_t s_runtime_health_state = RUNTIME_HEALTH_OK;

void runtime_health_init(void)
{
    s_runtime_health_state = RUNTIME_HEALTH_OK;
}

void runtime_health_set(runtime_health_state_t state)
{
    s_runtime_health_state = state;
}

runtime_health_state_t runtime_health_get(void)
{
    return s_runtime_health_state;
}
