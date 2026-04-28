#include "hal_gpio.h"

int hal_gpio_init(uint32_t pin, hal_gpio_direction_t direction)
{
    (void)pin;
    (void)direction;
    return 0;
}

int hal_gpio_write(uint32_t pin, bool level)
{
    (void)pin;
    (void)level;
    return 0;
}

int hal_gpio_read(uint32_t pin, bool* level)
{
    if (level != 0) {
        *level = false;
    }

    (void)pin;
    return 0;
}
