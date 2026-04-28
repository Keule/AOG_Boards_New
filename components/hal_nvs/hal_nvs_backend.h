#pragma once

#include <stddef.h>

int hal_nvs_backend_esp32_init(void);
int hal_nvs_backend_esp32_set_blob(const char* key, const void* data, size_t size);
int hal_nvs_backend_esp32_get_blob(const char* key, void* out_data, size_t* inout_size);
int hal_nvs_backend_sim_init(void);
int hal_nvs_backend_sim_set_blob(const char* key, const void* data, size_t size);
int hal_nvs_backend_sim_get_blob(const char* key, void* out_data, size_t* inout_size);
