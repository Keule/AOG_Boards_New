#include "hal_time_backend.h"

#include "esp_timer.h"

uint64_t hal_time_backend_esp32_us(void)
{
    return (uint64_t)esp_timer_get_time();
}
