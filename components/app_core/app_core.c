#include <stdio.h>
#include <stdarg.h>
#include "esp_log.h"
#include "app_core.h"
#include "feature_flags.h"
#include "board_profile.h"
#include "runtime.h"

static const char* TAG = "APP_CORE";

void app_core_init(void)
{
    ESP_LOGI(TAG, "System init");
}

void app_core_start(void)
{
    ESP_LOGI(TAG, "System start");

    runtime_init();
    runtime_start();
}
