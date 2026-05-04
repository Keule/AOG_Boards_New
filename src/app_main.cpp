#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_core.h"
#include "remote_log.h"

extern "C" void app_main(void)
{
    /* ---- EARLY BOOT LOG CAPTURE (NAV-REMOTELOG-BOOTBUFFER-001) ----
     * Install vprintf hook BEFORE any subsystem init.
     * Uses critical sections (no FreeRTOS primitives needed).
     * Captures all ESP_LOG output from this point forward,
     * including board_profile, HAL, Ethernet, GNSS boot logs. */
    remote_log_early_init();

    app_core_init();
    app_core_start();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
