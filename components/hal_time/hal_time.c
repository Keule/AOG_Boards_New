#include "hal_time.h"
#include "esp_timer.h"

static const hal_time_ops_t* s_time_ops = NULL;

hal_err_t hal_time_init(const hal_time_ops_t* ops)
{
    if (ops == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    s_time_ops = ops;
    return HAL_OK;
}

hal_err_t hal_time_deinit(void)
{
    s_time_ops = NULL;
    return HAL_OK;
}

uint64_t hal_time_us(void)
{
    if (s_time_ops == NULL || s_time_ops->time_us == NULL) {
        return 0;
    }
    return s_time_ops->time_us();
}

uint32_t hal_time_ms(void)
{
    if (s_time_ops == NULL || s_time_ops->time_ms == NULL) {
        return 0;
    }
    return s_time_ops->time_ms();
}

/* ---- ESP32 Stub Implementations ---- */

static hal_err_t esp32_time_init(void)
{
    return HAL_OK;
}

static hal_err_t esp32_time_deinit(void)
{
    return HAL_OK;
}

static uint64_t esp32_time_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

static uint32_t esp32_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static const hal_time_ops_t s_esp32_time_ops = {
    .init    = esp32_time_init,
    .deinit  = esp32_time_deinit,
    .time_us = esp32_time_us,
    .time_ms = esp32_time_ms,
};

const hal_time_ops_t* hal_time_esp32_ops(void)
{
    return &s_esp32_time_ops;
}
