#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_BACKEND_ESP32 = 0,
    HAL_BACKEND_SIM
} hal_backend_kind_t;

hal_backend_kind_t hal_backend_get_kind(void);

#ifdef __cplusplus
}
#endif
