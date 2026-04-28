#include "hal_gpio.h"

static const hal_gpio_ops_t* s_gpio_ops = NULL;

hal_err_t hal_gpio_init(const hal_gpio_ops_t* ops)
{
    if (ops == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    s_gpio_ops = ops;
    return HAL_OK;
}

hal_err_t hal_gpio_deinit(void)
{
    s_gpio_ops = NULL;
    return HAL_OK;
}

hal_err_t hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode)
{
    if (s_gpio_ops == NULL || s_gpio_ops->set_mode == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_gpio_ops->set_mode(pin, mode);
}

hal_err_t hal_gpio_set(uint8_t pin, hal_gpio_level_t level)
{
    if (s_gpio_ops == NULL || s_gpio_ops->set == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_gpio_ops->set(pin, level);
}

hal_gpio_level_t hal_gpio_get(uint8_t pin)
{
    if (s_gpio_ops == NULL || s_gpio_ops->get == NULL) {
        return HAL_GPIO_LOW;
    }
    return s_gpio_ops->get(pin);
}

/* ---- ESP32 Stub Implementations ---- */

static hal_err_t esp32_gpio_init(void)    { return HAL_OK; }
static hal_err_t esp32_gpio_deinit(void)  { return HAL_OK; }
static hal_err_t esp32_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode) { (void)pin; (void)mode; return HAL_OK; }
static hal_err_t esp32_gpio_set(uint8_t pin, hal_gpio_level_t level)      { (void)pin; (void)level; return HAL_OK; }
static hal_gpio_level_t esp32_gpio_get(uint8_t pin)                       { (void)pin; return HAL_GPIO_LOW; }

static const hal_gpio_ops_t s_esp32_gpio_ops = {
    .init     = esp32_gpio_init,
    .deinit   = esp32_gpio_deinit,
    .set_mode = esp32_gpio_set_mode,
    .set      = esp32_gpio_set,
    .get      = esp32_gpio_get,
};

const hal_gpio_ops_t* hal_gpio_esp32_ops(void)
{
    return &s_esp32_gpio_ops;
}
