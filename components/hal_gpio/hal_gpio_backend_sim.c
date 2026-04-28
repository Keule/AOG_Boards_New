#include "hal_gpio_backend.h"

int hal_gpio_backend_sim_init(uint32_t pin, hal_gpio_direction_t direction)
{
    (void)pin;
    (void)direction;
    return 0;
}

int hal_gpio_backend_sim_write(uint32_t pin, bool level)
{
    (void)pin;
    (void)level;
    return 0;
}

int hal_gpio_backend_sim_read(uint32_t pin, bool* level)
{
    (void)pin;
    if (level != 0) {
        *level = false;
    }
    return 0;
}
