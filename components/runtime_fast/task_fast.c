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

    while (1) {
        int64_t cycle_start_us = esp_timer_get_time();
        int64_t cycle_end_us = 0;
        uint32_t cycle_duration_us = 0;
        size_t component_count = runtime_component_count();
        size_t i = 0;

        ctx.cycle_id++;

        for (i = 0; i < component_count; i++) {
            runtime_component_t* component = runtime_component_get(i);

            if (component != NULL && component->fast_input != NULL) {
                component->fast_input(component, &ctx);
            }
        }

        for (i = 0; i < component_count; i++) {
            runtime_component_t* component = runtime_component_get(i);

            if (component != NULL && component->fast_process != NULL) {
                component->fast_process(component, &ctx);
            }
        }

        for (i = 0; i < component_count; i++) {
            runtime_component_t* component = runtime_component_get(i);

            if (component != NULL && component->fast_output != NULL) {
                component->fast_output(component, &ctx);
            }
        }

        cycle_end_us = esp_timer_get_time();

        if (cycle_end_us >= cycle_start_us) {
            cycle_duration_us = (uint32_t)(cycle_end_us - cycle_start_us);
        }

        runtime_stats_record(cycle_duration_us);
        ctx.timestamp_us = (uint64_t)cycle_end_us;
        ctx.period_us = 10000U;
        ctx.last_cycle_duration_us = runtime_stats_get_last();
        ctx.worst_cycle_duration_us = runtime_stats_get_worst();

        runtime_watchdog_feed("task_fast");

        vTaskDelay(pdMS_TO_TICKS(10));
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
