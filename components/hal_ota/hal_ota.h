#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int hal_ota_begin(size_t image_size);
int hal_ota_write(const uint8_t* data, size_t size);
int hal_ota_end(void);
int hal_ota_reboot(void);

#ifdef __cplusplus
}
#endif
