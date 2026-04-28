#include "hal_nvs.h"

static const hal_nvs_ops_t* s_nvs_ops = NULL;

hal_err_t hal_nvs_init(const hal_nvs_ops_t* ops)
{
    if (ops == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    s_nvs_ops = ops;
    return HAL_OK;
}

hal_err_t hal_nvs_deinit(void)
{
    s_nvs_ops = NULL;
    return HAL_OK;
}

hal_err_t hal_nvs_get(const char* key, void* out, size_t max_len, size_t* out_len)
{
    if (s_nvs_ops == NULL || s_nvs_ops->get == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_nvs_ops->get(key, out, max_len, out_len);
}

hal_err_t hal_nvs_set(const char* key, const void* value, size_t len)
{
    if (s_nvs_ops == NULL || s_nvs_ops->set == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_nvs_ops->set(key, value, len);
}

hal_err_t hal_nvs_erase(const char* key)
{
    if (s_nvs_ops == NULL || s_nvs_ops->erase == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_nvs_ops->erase(key);
}

/* ---- ESP32 Stub Implementations ---- */

static hal_err_t esp32_nvs_init(void)  { return HAL_OK; }
static hal_err_t esp32_nvs_deinit(void) { return HAL_OK; }

static hal_err_t esp32_nvs_get(const char* key, void* out, size_t max_len, size_t* out_len)
{
    (void)key; (void)out; (void)max_len;
    if (out_len != NULL) { *out_len = 0; }
    return HAL_OK;
}

static hal_err_t esp32_nvs_set(const char* key, const void* value, size_t len)
{
    (void)key; (void)value; (void)len;
    return HAL_OK;
}

static hal_err_t esp32_nvs_erase(const char* key)
{
    (void)key;
    return HAL_OK;
}

static const hal_nvs_ops_t s_esp32_nvs_ops = {
    .init  = esp32_nvs_init,
    .deinit = esp32_nvs_deinit,
    .get   = esp32_nvs_get,
    .set   = esp32_nvs_set,
    .erase = esp32_nvs_erase,
};

const hal_nvs_ops_t* hal_nvs_esp32_ops(void)
{
    return &s_esp32_nvs_ops;
}
