#pragma once

#include <stdint.h>
#include <stddef.h>
#include "hal_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- HAL NVS Ops ---- */

typedef struct {
    hal_err_t (*init)(void);
    hal_err_t (*deinit)(void);
    hal_err_t (*get)(const char* key, void* out, size_t max_len, size_t* out_len);
    hal_err_t (*set)(const char* key, const void* value, size_t len);
    hal_err_t (*erase)(const char* key);
} hal_nvs_ops_t;

/* ---- HAL NVS API ---- */

hal_err_t hal_nvs_init(const hal_nvs_ops_t* ops);
hal_err_t hal_nvs_deinit(void);
hal_err_t hal_nvs_get(const char* key, void* out, size_t max_len, size_t* out_len);
hal_err_t hal_nvs_set(const char* key, const void* value, size_t len);
hal_err_t hal_nvs_erase(const char* key);
const hal_nvs_ops_t* hal_nvs_esp32_ops(void);

#ifdef __cplusplus
}
#endif
