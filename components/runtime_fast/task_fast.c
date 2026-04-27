#include "task_fast.h"
#include "runtime_types.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void task_fast(void* arg)
{
    fast_cycle_context_t ctx = {0};

    while (1) {
        ctx.cycle_id++;

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
