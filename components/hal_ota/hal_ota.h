#pragma once

#include <stdint.h>
#include <stddef.h>
#include "hal_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- HAL OTA Ops ---- */

typedef struct {
    hal_err_t (*begin)(size_t image_size);
    hal_err_t (*write)(const uint8_t* data, size_t len);
    hal_err_t (*end)(void);
    hal_err_t (*reboot)(void);
} hal_ota_ops_t;

/* ---- HAL OTA API ---- */

hal_err_t hal_ota_init(const hal_ota_ops_t* ops);
hal_err_t hal_ota_begin(size_t image_size);
hal_err_t hal_ota_write(const uint8_t* data, size_t len);
hal_err_t hal_ota_end(void);
hal_err_t hal_ota_reboot(void);
const hal_ota_ops_t* hal_ota_esp32_ops(void);

#ifdef __cplusplus
}
#endif
