#include "task_fast.h"
#include "runtime_types.h"
#include "runtime_component.h"
#include "runtime_stats.h"
#include "runtime_watchdog.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void task_fast(void* arg)
{
    fast_cycle_context_t ctx = {0};

    runtime_stats_init();
    runtime_watchdog_init();
    runtime_watchdog_register_task("task_fast");

    /* NAV-FIX-001 AP-C: vTaskDelayUntil for deterministic 100 Hz (10ms period).
     * vTaskDelay() causes cumulative jitter — each cycle starts 10ms AFTER
     * the previous cycle ENDED, not 10ms after it STARTED.
     * vTaskDelayUntil() starts each cycle at a fixed interval from the
     * previous cycle START, eliminating jitter accumulation. */
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period_ticks = pdMS_TO_TICKS(10);

    while (1) {
        int64_t cycle_start_us = esp_timer_get_time();
        size_t component_count = runtime_component_count();
        size_t i = 0;

        /* NAV-FIX-001-R2 AP-D: Set timestamp and period BEFORE fast hooks.
         * Components need valid ctx.timestamp_us and ctx.period_us
         * during their fast_input/fast_process/fast_output callbacks. */
        ctx.timestamp_us = (uint64_t)cycle_start_us;
        ctx.period_us = 10000U;
        ctx.cycle_id++;

        /* ---- Phase 1: fast_input ---- */
        for (i = 0; i < component_count; i++) {
            runtime_component_t* component = runtime_component_get(i);

            if (component != NULL && component->fast_input != NULL) {
                component->fast_input(component, &ctx);
            }
        }

        /* ---- Phase 2: fast_process ---- */
        for (i = 0; i < component_count; i++) {
            runtime_component_t* component = runtime_component_get(i);

            if (component != NULL && component->fast_process != NULL) {
                component->fast_process(component, &ctx);
            }
        }

        /* ---- Phase 3: fast_output ---- */
        for (i = 0; i < component_count; i++) {
            runtime_component_t* component = runtime_component_get(i);

            if (component != NULL && component->fast_output != NULL) {
                component->fast_output(component, &ctx);
            }
        }

        /* ---- Post-hook measurements ---- */
        int64_t cycle_end_us = esp_timer_get_time();

        if (cycle_end_us >= cycle_start_us) {
            uint32_t cycle_duration_us = (uint32_t)(cycle_end_us - cycle_start_us);
            runtime_stats_record(cycle_duration_us);
        }

        ctx.last_cycle_duration_us = runtime_stats_get_last();
        ctx.worst_cycle_duration_us = runtime_stats_get_worst();

        runtime_watchdog_feed("task_fast");

        /* NAV-FIX-001 AP-C: Deadline miss detection.
         * Record expected wake time BEFORE the delay, then check actual
         * wake time AFTER. If actual > expected, we missed the deadline. */
        TickType_t expected_wake = last_wake_time + period_ticks;

        vTaskDelayUntil(&last_wake_time, period_ticks);

        TickType_t actual_wake = xTaskGetTickCount();
        if (actual_wake > expected_wake) {
            runtime_stats_record_deadline_miss();
        }
    }
}

void task_fast_start(void)
{
    xTaskCreatePinnedToCore(
        task_fast,
        "task_fast",
        4096,
        NULL,
        10,
        NULL,
        1
    );
}
