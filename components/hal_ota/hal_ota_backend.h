#pragma once

#include <stddef.h>
#include <stdint.h>

int hal_ota_backend_esp32_begin(size_t image_size);
int hal_ota_backend_esp32_write(const uint8_t* data, size_t size);
int hal_ota_backend_esp32_end(void);
int hal_ota_backend_esp32_reboot(void);
int hal_ota_backend_sim_begin(size_t image_size);
int hal_ota_backend_sim_write(const uint8_t* data, size_t size);
int hal_ota_backend_sim_end(void);
int hal_ota_backend_sim_reboot(void);
