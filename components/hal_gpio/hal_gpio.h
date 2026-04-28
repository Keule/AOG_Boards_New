#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_GPIO_DIR_IN = 0,
    HAL_GPIO_DIR_OUT
} hal_gpio_direction_t;

int hal_gpio_init(uint32_t pin, hal_gpio_direction_t direction);
int hal_gpio_write(uint32_t pin, bool level);
int hal_gpio_read(uint32_t pin, bool* level);

#ifdef __cplusplus
}
#endif
