#include "runtime_stats.h"

#define RUNTIME_STATS_WINDOW_SIZE 1000

static uint32_t s_window[RUNTIME_STATS_WINDOW_SIZE];
static uint32_t s_index = 0;
static uint32_t s_count = 0;
static uint32_t s_last = 0;
static uint32_t s_worst = 0;

void runtime_stats_init(void)
{
    uint32_t i = 0;

    for (i = 0; i < RUNTIME_STATS_WINDOW_SIZE; i++) {
        s_window[i] = 0;
    }

    s_index = 0;
    s_count = 0;
    s_last = 0;
    s_worst = 0;
}

void runtime_stats_record(uint32_t cycle_duration_us)
{
    uint32_t i = 0;
    uint32_t local_worst = 0;

    s_window[s_index] = cycle_duration_us;
    s_index = (s_index + 1U) % RUNTIME_STATS_WINDOW_SIZE;

    if (s_count < RUNTIME_STATS_WINDOW_SIZE) {
        s_count++;
    }

    s_last = cycle_duration_us;

    for (i = 0; i < s_count; i++) {
        if (s_window[i] > local_worst) {
            local_worst = s_window[i];
        }
    }

    s_worst = local_worst;
}

uint32_t runtime_stats_get_last(void)
{
    return s_last;
}

uint32_t runtime_stats_get_worst(void)
{
    return s_worst;
}
