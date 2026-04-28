#include "hal_reset.h"

static const hal_reset_ops_t* s_reset_ops = NULL;

hal_err_t hal_reset_init(const hal_reset_ops_t* ops)
{
    if (ops == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    s_reset_ops = ops;
    return HAL_OK;
}

hal_err_t hal_reset_software_reset(void)
{
    if (s_reset_ops == NULL || s_reset_ops->software_reset == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_reset_ops->software_reset();
}

/* ---- ESP32 Stub Implementations ---- */

static hal_err_t esp32_reset_init(void)            { return HAL_OK; }
static hal_err_t esp32_reset_software_reset(void)  { return HAL_OK; }

static const hal_reset_ops_t s_esp32_reset_ops = {
    .init            = esp32_reset_init,
    .software_reset  = esp32_reset_software_reset,
};

const hal_reset_ops_t* hal_reset_esp32_ops(void)
{
    return &s_esp32_reset_ops;
}
