#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void runtime_stats_init(void);
void runtime_stats_record(uint32_t cycle_duration_us);
uint32_t runtime_stats_get_last(void);
uint32_t runtime_stats_get_worst(void);
uint32_t runtime_stats_get_cycle_count(void);

/* NAV-FIX-001 AP-C: Deadline miss tracking for task_fast.
 * A deadline miss occurs when the actual wake time exceeds
 * the expected wake time (vTaskDelayUntil returned late). */
void runtime_stats_record_deadline_miss(void);
uint32_t runtime_stats_get_deadline_miss_count(void);

#ifdef __cplusplus
}
#endif
