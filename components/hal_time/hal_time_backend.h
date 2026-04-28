#pragma once

#include <stdint.h>

uint64_t hal_time_backend_esp32_us(void);
uint64_t hal_time_backend_sim_us(void);
