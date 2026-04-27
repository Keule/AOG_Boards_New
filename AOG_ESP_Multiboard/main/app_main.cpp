#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_core.h"

extern "C" void app_main(void)
{
    app_core_init();
    app_core_start();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
