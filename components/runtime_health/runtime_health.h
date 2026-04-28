#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RUNTIME_HEALTH_OK = 0,
    RUNTIME_HEALTH_WARN,
    RUNTIME_HEALTH_ERROR,
    RUNTIME_HEALTH_FATAL
} runtime_health_state_t;

void runtime_health_init(void);
void runtime_health_set(runtime_health_state_t state);
runtime_health_state_t runtime_health_get(void);

#ifdef __cplusplus
}
#endif
