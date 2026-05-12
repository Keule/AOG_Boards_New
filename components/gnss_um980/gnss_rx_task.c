#include <stdio.h>

#include "gnss_rx_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "GNSS_RX_TASK";

#define GNSS_RX_TASK_STACK_SIZE  4096
#define GNSS_RX_TASK_PRIORITY     5
#define GNSS_RX_TASK_CORE         0

extern void gnss_um980_consume_ringbuffer(gnss_um980_t* rx);

static void gnss_rx_task_fn(void* arg)
{
    gnss_um980_t* rx = (gnss_um980_t*)arg;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1));

        if (rx->rx_source == NULL) {
            continue;
        }

        gnss_um980_consume_ringbuffer(rx);
    }
}

BaseType_t gnss_rx_task_start(gnss_um980_t* rx, const char* name)
{
    char task_name[32];
    const char* task_name_to_use;

    if (rx == NULL) {
        ESP_LOGE(TAG, "Failed to start GNSS RX task: invalid receiver");
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }

    if (name != NULL && name[0] != '\0') {
        snprintf(task_name, sizeof(task_name), "%s", name);
    } else {
        snprintf(task_name, sizeof(task_name), "gnss_rx%u", (unsigned)(rx->instance_id + 1U));
    }
    task_name_to_use = task_name;

    if (xTaskCreatePinnedToCore(
            gnss_rx_task_fn,
            task_name_to_use,
            GNSS_RX_TASK_STACK_SIZE,
            rx,
            GNSS_RX_TASK_PRIORITY,
            NULL,
            GNSS_RX_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start GNSS RX task: %s", task_name_to_use);
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }

    return pdPASS;
}