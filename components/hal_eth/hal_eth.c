#include "hal_eth.h"

static const hal_eth_ops_t* s_eth_ops = NULL;

hal_err_t hal_eth_init(const hal_eth_ops_t* ops)
{
    if (ops == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    s_eth_ops = ops;
    return HAL_OK;
}

hal_err_t hal_eth_deinit(void)
{
    s_eth_ops = NULL;
    return HAL_OK;
}

hal_err_t hal_eth_get_mac(uint8_t mac[6])
{
    if (s_eth_ops == NULL || s_eth_ops->get_mac == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_eth_ops->get_mac(mac);
}

bool hal_eth_is_connected(void)
{
    if (s_eth_ops == NULL || s_eth_ops->is_connected == NULL) {
        return false;
    }
    return s_eth_ops->is_connected();
}

ethernet_kind_t hal_eth_get_controller(void)
{
    return board_profile_get_eth();
}

/* ---- ESP32 Stub Implementations ---- */

static hal_err_t esp32_eth_init(void)             { return HAL_OK; }
static hal_err_t esp32_eth_deinit(void)            { return HAL_OK; }
static hal_err_t esp32_eth_get_mac(uint8_t mac[6]) { for (int i = 0; i < 6; i++) { mac[i] = 0; } return HAL_OK; }
static bool esp32_eth_is_connected(void)           { return false; }

static const hal_eth_ops_t s_esp32_eth_ops = {
    .init         = esp32_eth_init,
    .deinit       = esp32_eth_deinit,
    .get_mac      = esp32_eth_get_mac,
    .is_connected = esp32_eth_is_connected,
};

const hal_eth_ops_t* hal_eth_esp32_ops(void)
{
    return &s_esp32_eth_ops;
}
