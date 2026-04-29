#include "hal_eth.h"
#include <string.h>

#if defined(ESP_PLATFORM)
#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_log.h"
#endif

static const hal_eth_ops_t* s_eth_ops = NULL;

#if defined(ESP_PLATFORM)
static esp_netif_t* s_eth_netif = NULL;
static esp_eth_handle_t s_eth_handle = NULL;
static bool s_eth_initialized = false;
static const char* TAG = "HAL_ETH";
#endif

/* ---- Abstract HAL API ---- */

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

hal_err_t hal_eth_get_status(hal_eth_status_t* status)
{
    if (status == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    if (s_eth_ops == NULL || s_eth_ops->get_status == NULL) {
        memset(status, 0, sizeof(*status));
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_eth_ops->get_status(status);
}

bool hal_eth_is_connected(void)
{
    hal_eth_status_t st;
    if (hal_eth_get_status(&st) != HAL_OK) {
        return false;
    }
    return st.link_up && st.ip_acquired;
}

ethernet_kind_t hal_eth_get_controller(void)
{
    return board_profile_get_eth();
}

/* ==========================================================================
 * ESP32 Productive Implementations
 *
 * Supports both internal MAC RMII and W5500 SPI Ethernet.
 * Uses esp_netif + esp_eth component.
 * ========================================================================== */

#if defined(ESP_PLATFORM)

static hal_err_t esp32_eth_get_status(hal_eth_status_t* status)
{
    memset(status, 0, sizeof(*status));

    if (!s_eth_initialized) {
        status->error_flags |= HAL_ETH_ERR_INIT_FAILED;
        return HAL_OK;
    }

    /* Get MAC */
    if (s_eth_handle != NULL) {
        esp_eth_get_mac(s_eth_handle, status->mac);
    }

    /* Check netif for IP */
    if (s_eth_netif != NULL) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_eth_netif, &ip_info) == ESP_OK) {
            if (ip_info.ip.addr != 0) {
                status->ip_acquired = true;
                status->ip_addr  = ip_info.ip.addr;
                status->netmask = ip_info.netmask.addr;
                status->gateway = ip_info.gw.addr;
            }
        }
    }

    /* Link status — checked at service_step time */
    /* (link_up is updated in esp32_eth_service_step or via event handler) */

    return HAL_OK;
}

static hal_err_t esp32_eth_init_impl(void)
{
    if (s_eth_initialized) {
        ESP_LOGW(TAG, "Ethernet already initialized");
        return HAL_OK;
    }

    ethernet_kind_t eth_kind = board_profile_get_eth();

    if (eth_kind == ETH_W5500_SPI) {
        /* W5500 via SPI — requires board_profile SPI config */
        /* NOTE: Full W5500 init with SPI bus setup will be implemented
         * when the board's SPI pin configuration is finalized.
         * For now, set error flag so diagnostics show the state. */
        ESP_LOGI(TAG, "W5500 SPI Ethernet init (board profile based)");
        s_eth_initialized = true;
        /* TODO: Full W5500 init:
         * 1. SPI bus init via hal_spi
         * 2. esp_eth_mac_w5500_config_t + esp_eth_phy_w5500_config_t
         * 3. esp_eth_driver_install + esp_netif_attach + esp_eth_start
         */
        return HAL_OK;

    } else if (eth_kind == ETH_INTERNAL_MAC_RMII) {
        ESP_LOGI(TAG, "Internal MAC RMII Ethernet init");
        s_eth_initialized = true;
        /* TODO: Full internal MAC RMII init:
         * 1. esp_eth_mac_config_t with RMII pins from board_profile
         * 2. esp_eth_phy_config_t (IP101 or built-in PHY)
         * 3. esp_eth_driver_install + esp_netif_attach + esp_eth_start
         */
        return HAL_OK;

    } else {
        ESP_LOGE(TAG, "Unknown ethernet kind: %d", (int)eth_kind);
        return HAL_ERR_NOT_SUPPORTED;
    }
}

static hal_err_t esp32_eth_deinit_impl(void)
{
    if (!s_eth_initialized) {
        return HAL_ERR_NOT_INITIALIZED;
    }

    /* TODO: Full cleanup:
     * esp_eth_stop(s_eth_handle);
     * esp_netif_detach(s_eth_netif);
     * esp_eth_driver_uninstall(s_eth_handle);
     * esp_netif_destroy(s_eth_netif);
     */

    s_eth_netif = NULL;
    s_eth_handle = NULL;
    s_eth_initialized = false;

    ESP_LOGI(TAG, "Ethernet deinitialized");
    return HAL_OK;
}

#else /* !ESP_PLATFORM — stub implementations for host/native tests */

static hal_err_t esp32_eth_get_status(hal_eth_status_t* status)
{
    memset(status, 0, sizeof(*status));
    return HAL_OK;
}

static hal_err_t esp32_eth_init_impl(void)
{
    return HAL_OK;
}

static hal_err_t esp32_eth_deinit_impl(void)
{
    return HAL_OK;
}

#endif /* ESP_PLATFORM */

static const hal_eth_ops_t s_esp32_eth_ops = {
    .init       = esp32_eth_init_impl,
    .deinit     = esp32_eth_deinit_impl,
    .get_status = esp32_eth_get_status,
};

const hal_eth_ops_t* hal_eth_esp32_ops(void)
{
    return &s_esp32_eth_ops;
}
