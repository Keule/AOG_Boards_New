#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal_gpio.h"

int hal_gpio_backend_esp32_init(uint32_t pin, hal_gpio_direction_t direction);
int hal_gpio_backend_esp32_write(uint32_t pin, bool level);
int hal_gpio_backend_esp32_read(uint32_t pin, bool* level);
int hal_gpio_backend_sim_init(uint32_t pin, hal_gpio_direction_t direction);
int hal_gpio_backend_sim_write(uint32_t pin, bool level);
int hal_gpio_backend_sim_read(uint32_t pin, bool* level);
