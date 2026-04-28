#include "runtime_stats.h"

#define RUNTIME_STATS_BUFFER_SIZE 1000

static uint32_t s_durations[RUNTIME_STATS_BUFFER_SIZE];
static uint32_t s_last = 0;
static uint32_t s_worst = 0;
static uint32_t s_index = 0;

void runtime_stats_init(void)
{
    uint32_t i = 0;

    for (i = 0; i < RUNTIME_STATS_BUFFER_SIZE; i++) {
        s_durations[i] = 0;
    }

    s_last = 0;
    s_worst = 0;
    s_index = 0;
}

void runtime_stats_record(uint32_t cycle_duration_us)
{
    s_durations[s_index] = cycle_duration_us;
    s_last = cycle_duration_us;

    if (cycle_duration_us > s_worst) {
        s_worst = cycle_duration_us;
    }

    s_index++;

    if (s_index >= RUNTIME_STATS_BUFFER_SIZE) {
        s_index = 0;
    }
}

uint32_t runtime_stats_get_last(void)
{
    return s_last;
}

uint32_t runtime_stats_get_worst(void)
{
    return s_worst;
}
