#include "runtime.h"
#include "runtime_component.h"
#include "task_fast.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ---- Service task configuration ---- */

#define SERVICE_STACK_SIZE  4096
#define SERVICE_PRIORITY    5
#define SERVICE_INTERVAL_MS 10

static void task_service(void* arg)
{
    (void)arg;

    for (;;) {
        int64_t now = esp_timer_get_time();
        runtime_service_step_all((uint64_t)now);
        vTaskDelay(pdMS_TO_TICKS(SERVICE_INTERVAL_MS));
    }
}

/* ---- Public API ---- */

void runtime_init(void)
{
}

void runtime_start(void)
{
    task_fast_start();

    /* Service task on Core 0: iterates all registered components
     * and calls their subsystem-local service_step() callbacks.
     * No business logic here — pure dispatch. */
    xTaskCreatePinnedToCore(
        task_service,
        "service",
        SERVICE_STACK_SIZE,
        NULL,
        SERVICE_PRIORITY,
        NULL,
        0   /* Core 0: separate from task_fast on Core 1 */
    );
}
