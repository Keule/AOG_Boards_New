#pragma once

#include <stdint.h>
#include "hal_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- GPIO Pin Mode ---- */

typedef enum {
    HAL_GPIO_MODE_INPUT = 0,
    HAL_GPIO_MODE_OUTPUT,
    HAL_GPIO_MODE_OUTPUT_OD,
    HAL_GPIO_MODE_INPUT_PULLUP,
    HAL_GPIO_MODE_INPUT_PULLDOWN
} hal_gpio_mode_t;

/* ---- GPIO Level ---- */

typedef enum {
    HAL_GPIO_LOW = 0,
    HAL_GPIO_HIGH = 1
} hal_gpio_level_t;

/* ---- HAL GPIO Ops ---- */

typedef struct {
    hal_err_t (*init)(void);
    hal_err_t (*deinit)(void);
    hal_err_t (*set_mode)(uint8_t pin, hal_gpio_mode_t mode);
    hal_err_t (*set)(uint8_t pin, hal_gpio_level_t level);
    hal_gpio_level_t (*get)(uint8_t pin);
} hal_gpio_ops_t;

/* ---- HAL GPIO API ---- */

hal_err_t hal_gpio_init(const hal_gpio_ops_t* ops);
hal_err_t hal_gpio_deinit(void);
hal_err_t hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode);
hal_err_t hal_gpio_set(uint8_t pin, hal_gpio_level_t level);
hal_gpio_level_t hal_gpio_get(uint8_t pin);
const hal_gpio_ops_t* hal_gpio_esp32_ops(void);

#ifdef __cplusplus
}
#endif
