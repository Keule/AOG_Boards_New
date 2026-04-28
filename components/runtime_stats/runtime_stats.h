#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void runtime_stats_init(void);
void runtime_stats_record(uint32_t cycle_duration_us);
uint32_t runtime_stats_get_last(void);
uint32_t runtime_stats_get_worst(void);

#ifdef __cplusplus
}
#endif
