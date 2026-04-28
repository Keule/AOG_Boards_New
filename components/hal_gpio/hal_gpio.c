#include "hal_gpio.h"

#include "hal_backend.h"
#include "hal_gpio_backend.h"

int hal_gpio_init(uint32_t pin, hal_gpio_direction_t direction)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_gpio_backend_sim_init(pin, direction);
    }

    return hal_gpio_backend_esp32_init(pin, direction);
}

int hal_gpio_write(uint32_t pin, bool level)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_gpio_backend_sim_write(pin, level);
    }

    return hal_gpio_backend_esp32_write(pin, level);
}

int hal_gpio_read(uint32_t pin, bool* level)
{
    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_gpio_backend_sim_read(pin, level);
    }

    return hal_gpio_backend_esp32_read(pin, level);
}
