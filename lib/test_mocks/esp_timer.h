#pragma once
/*
 * Stub esp_timer.h for native tests.
 * Replaces ESP-IDF's high-resolution timer with a simple mock.
 */

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stub: returns elapsed microseconds since program start.
 * Uses clock_gettime(CLOCK_MONOTONIC) for reasonable accuracy. */
static inline int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
}

#ifdef __cplusplus
}
#endif
