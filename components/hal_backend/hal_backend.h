#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- HAL Backend Type ---- */

typedef enum {
    HAL_BACKEND_NONE = 0,
    HAL_BACKEND_ESP32,
    HAL_BACKEND_SIM
} hal_backend_type_t;

/* ---- Common HAL Error Codes ---- */

typedef enum {
    HAL_OK = 0,
    HAL_ERR_NOT_INITIALIZED,
    HAL_ERR_INVALID_PARAM,
    HAL_ERR_NOT_SUPPORTED,
    HAL_ERR_BUSY,
    HAL_ERR_TIMEOUT,
    HAL_ERR_IO,
    HAL_ERR_NO_MEMORY
} hal_err_t;

/* ---- Backend Registration ---- */

/* Set the active HAL backend type.
 * For diagnostics only; each HAL module manages its own ops. */
void hal_backend_set(hal_backend_type_t backend);

/* Get the active HAL backend type. */
hal_backend_type_t hal_backend_get(void);

#ifdef __cplusplus
}
#endif
