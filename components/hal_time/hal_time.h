#pragma once

#include <stdint.h>
#include "hal_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- HAL Time Ops ---- */

typedef struct {
    hal_err_t (*init)(void);
    hal_err_t (*deinit)(void);
    uint64_t (*time_us)(void);
    uint32_t (*time_ms)(void);
} hal_time_ops_t;

/* ---- HAL Time API ---- */

/* Set time HAL ops (ESP32 or SIM backend). */
hal_err_t hal_time_init(const hal_time_ops_t* ops);

/* Deinitialize time HAL. */
hal_err_t hal_time_deinit(void);

/* Get current time in microseconds. */
uint64_t hal_time_us(void);

/* Get current time in milliseconds. */
uint32_t hal_time_ms(void);

/* Convenience: get pre-configured ESP32 stub ops. */
const hal_time_ops_t* hal_time_esp32_ops(void);

#ifdef __cplusplus
}
#endif
