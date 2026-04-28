#include "hal_time.h"

#include "esp_timer.h"

uint64_t hal_time_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

uint32_t hal_time_ms(void)
{
    return (uint32_t)(hal_time_us() / 1000ULL);
}
