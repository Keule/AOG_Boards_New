#pragma once

#include "hal_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- HAL Reset Ops ---- */

typedef struct {
    hal_err_t (*init)(void);
    hal_err_t (*software_reset)(void);
} hal_reset_ops_t;

/* ---- HAL Reset API ---- */

hal_err_t hal_reset_init(const hal_reset_ops_t* ops);
hal_err_t hal_reset_software_reset(void);
const hal_reset_ops_t* hal_reset_esp32_ops(void);

#ifdef __cplusplus
}
#endif
