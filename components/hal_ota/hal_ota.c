#include "hal_ota.h"

static const hal_ota_ops_t* s_ota_ops = NULL;

hal_err_t hal_ota_init(const hal_ota_ops_t* ops)
{
    if (ops == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    s_ota_ops = ops;
    return HAL_OK;
}

hal_err_t hal_ota_begin(size_t image_size)
{
    if (s_ota_ops == NULL || s_ota_ops->begin == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_ota_ops->begin(image_size);
}

hal_err_t hal_ota_write(const uint8_t* data, size_t len)
{
    if (s_ota_ops == NULL || s_ota_ops->write == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_ota_ops->write(data, len);
}

hal_err_t hal_ota_end(void)
{
    if (s_ota_ops == NULL || s_ota_ops->end == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_ota_ops->end();
}

hal_err_t hal_ota_reboot(void)
{
    if (s_ota_ops == NULL || s_ota_ops->reboot == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_ota_ops->reboot();
}

/* ---- ESP32 Stub Implementations ---- */

static hal_err_t esp32_ota_begin(size_t image_size)  { (void)image_size; return HAL_OK; }
static hal_err_t esp32_ota_write(const uint8_t* data, size_t len) { (void)data; (void)len; return HAL_OK; }
static hal_err_t esp32_ota_end(void)                 { return HAL_OK; }
static hal_err_t esp32_ota_reboot(void)              { return HAL_OK; }

static const hal_ota_ops_t s_esp32_ota_ops = {
    .begin  = esp32_ota_begin,
    .write  = esp32_ota_write,
    .end    = esp32_ota_end,
    .reboot = esp32_ota_reboot,
};

const hal_ota_ops_t* hal_ota_esp32_ops(void)
{
    return &s_esp32_ota_ops;
}
