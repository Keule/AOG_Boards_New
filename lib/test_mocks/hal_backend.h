#pragma once
/*
 * Stub hal_backend.h for native tests.
 * Only provides the type definitions needed by transport_uart/transport_tcp.
 * Does NOT provide the real HAL functions (those are mocked per-test).
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif
