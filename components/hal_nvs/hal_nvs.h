#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int hal_nvs_init(void);
int hal_nvs_set_blob(const char* key, const void* data, size_t size);
int hal_nvs_get_blob(const char* key, void* out_data, size_t* inout_size);

#ifdef __cplusplus
}
#endif
