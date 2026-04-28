#include "task_service.h"

#include "runtime_component.h"
#include "runtime_watchdog.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void task_service(void* arg)
{
    (void)arg;

    runtime_watchdog_register_task("task_service");

    while (1) {
        uint64_t timestamp_us = (uint64_t)esp_timer_get_time();
        size_t component_count = runtime_component_count();
        size_t i = 0;

        for (i = 0; i < component_count; ++i) {
            runtime_component_t* component = runtime_component_get(i);

            if (component != NULL && component->service_step != NULL) {
                component->service_step(component, timestamp_us);
            }
        }

        runtime_watchdog_feed("task_service");
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void task_service_start(void)
{
    xTaskCreatePinnedToCore(
        task_service,
        "task_service",
        4096,
        NULL,
        9,
        NULL,
        1
    );
}
