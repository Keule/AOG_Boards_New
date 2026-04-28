#include "task_fast.h"
#include "runtime_types.h"
#include "runtime_component.h"
#include "runtime_stats.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void task_fast(void* arg)
{
    fast_cycle_context_t ctx = {0};

    runtime_stats_init();

    while (1) {
        size_t component_count = runtime_component_count();
        size_t i = 0;
        TickType_t cycle_start_tick = xTaskGetTickCount();

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

        {
            TickType_t cycle_end_tick = xTaskGetTickCount();
            uint32_t cycle_duration_us =
                (uint32_t)(cycle_end_tick - cycle_start_tick) * (uint32_t)portTICK_PERIOD_MS * 1000U;

            runtime_stats_record(cycle_duration_us);
            ctx.last_cycle_duration_us = runtime_stats_get_last();
            ctx.worst_cycle_duration_us = runtime_stats_get_worst();
        }

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
