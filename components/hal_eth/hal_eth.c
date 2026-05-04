/* ========================================================================
 * hal_eth.c — ESP32 RMII Ethernet HAL (NAV-ETH-BRINGUP-001)
 *
 * Replaces previous stub with real ESP-IDF Ethernet driver:
 *   - RTL8201 PHY via RMII (generic 802.3 PHY driver)
 *   - GPIO0 input clock, MDC=23, MDIO=18, Power=12
 *   - DHCP for IP assignment
 *   - Event handlers for link/connect/disconnect/GOT_IP
 *
 * ESP-IDF 6.x API:
 *   - esp_eth_mac_new_esp32(esp32_config, mac_config)
 *   - esp_eth_phy_new_generic(phy_config)
 *   - esp_eth_new_netif_glue(eth_handle)
 *   - esp_netif_attach(netif, glue)
 *   - esp_eth_driver_install / esp_eth_start
 *
 * Pattern: hal_eth_ops_t vtable (same as hal_uart)
 * ======================================================================== */

#include "hal_eth.h"
#include "esp_log.h"
#include "esp_eth.h"
#include "esp_eth_netif_glue.h"
#include "esp_eth_mac_esp.h"
#include "esp_eth_phy.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "HAL_ETH";

/* ---- Global ETH state ---- */
static const hal_eth_ops_t* s_eth_ops = NULL;
static hal_eth_status_t      s_eth_status;
static esp_eth_handle_t      s_eth_handle = NULL;
static esp_netif_t*          s_netif = NULL;
static esp_eth_netif_glue_handle_t s_netif_glue = NULL;

/* ---- Standard HAL ops dispatch ---- */

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
    /* Return real link status if initialized */
    if (s_eth_status.initialized) {
        return s_eth_status.link_up && s_eth_status.got_ip;
    }
    /* Fall back to ops vtable if set */
    if (s_eth_ops == NULL || s_eth_ops->is_connected == NULL) {
        return false;
    }
    return s_eth_ops->is_connected();
}

ethernet_kind_t hal_eth_get_controller(void)
{
    return board_profile_get_eth();
}

const hal_eth_status_t* hal_eth_get_status(void)
{
    return &s_eth_status;
}

/* =================================================================
 * ESP-IDF Ethernet Event Handlers
 * ================================================================= */

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_START:
                ESP_LOGI(TAG, "ETH: state=started");
                break;
            case ETHERNET_EVENT_STOP:
                ESP_LOGI(TAG, "ETH: state=stopped");
                s_eth_status.link_up = false;
                break;
            case ETHERNET_EVENT_CONNECTED:
                ESP_LOGI(TAG, "ETH: link=up");
                s_eth_status.link_up = true;
                break;
            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "ETH: link=down");
                s_eth_status.link_up = false;
                s_eth_status.got_ip = false;
                memset(s_eth_status.ip_addr, 0, 4);
                memset(s_eth_status.netmask, 0, 4);
                memset(s_eth_status.gw, 0, 4);
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_ETH_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            char ip_buf[16];
            ESP_LOGI(TAG, "ETH: got_ip=%s",
                     esp_ip4addr_ntoa(&event->ip_info.ip, ip_buf, sizeof(ip_buf)));
            s_eth_status.got_ip = true;
            s_eth_status.ip_addr[0] = esp_ip4_addr1(&event->ip_info.ip);
            s_eth_status.ip_addr[1] = esp_ip4_addr2(&event->ip_info.ip);
            s_eth_status.ip_addr[2] = esp_ip4_addr3(&event->ip_info.ip);
            s_eth_status.ip_addr[3] = esp_ip4_addr4(&event->ip_info.ip);
            s_eth_status.netmask[0] = esp_ip4_addr1(&event->ip_info.netmask);
            s_eth_status.netmask[1] = esp_ip4_addr2(&event->ip_info.netmask);
            s_eth_status.netmask[2] = esp_ip4_addr3(&event->ip_info.netmask);
            s_eth_status.netmask[3] = esp_ip4_addr4(&event->ip_info.netmask);
            s_eth_status.gw[0] = esp_ip4_addr1(&event->ip_info.gw);
            s_eth_status.gw[1] = esp_ip4_addr2(&event->ip_info.gw);
            s_eth_status.gw[2] = esp_ip4_addr3(&event->ip_info.gw);
            s_eth_status.gw[3] = esp_ip4_addr4(&event->ip_info.gw);
        }
    }
}

/* =================================================================
 * ESP32 Real Ethernet Implementation (ESP-IDF 6.x API)
 * ================================================================= */

#if !defined(UNIT_TEST) && !defined(NATIVE_TEST)

static hal_err_t esp32_eth_init_real(void)
{
    memset(&s_eth_status, 0, sizeof(s_eth_status));

    /* Read board profile ETH pins */
    board_eth_pins_t pins;
    if (!board_profile_get_eth_pins(&pins)) {
        ESP_LOGE(TAG, "ETH_ERROR: no ETH pins in board profile");
        return HAL_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(TAG, "ETH: init driver=RTL8201/RMII clk=GPIO0_IN mdc=%d mdio=%d power=%d reset=%d",
             pins.mdc_pin, pins.mdio_pin, pins.power_pin, pins.reset_pin);

    /* ---- WP-C: PHY Power GPIO12 ---- */
    if (pins.power_pin >= 0) {
        gpio_set_direction((gpio_num_t)pins.power_pin, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)pins.power_pin, 1);
        ESP_LOGI(TAG, "ETH: power gpio=%d level=1", pins.power_pin);
        /* RTL8201 needs time after power-on */
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* ---- TCP/IP stack and event loop ---- */
    esp_netif_init();
    esp_event_loop_create_default();

    /* ---- Create netif for Ethernet ---- */
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    s_netif = esp_netif_new(&cfg);
    if (s_netif == NULL) {
        ESP_LOGE(TAG, "ETH_ERROR: netif_init_failed");
        return HAL_ERR_IO;
    }

    /* ---- MAC config (ESP32 internal EMAC) ---- */
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t esp32_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    /* Override with board profile pins */
    esp32_config.smi_gpio.mdc_num  = pins.mdc_pin;
    esp32_config.smi_gpio.mdio_num = pins.mdio_pin;
    esp32_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    esp32_config.clock_config.rmii.clock_gpio  = 0; /* GPIO0 for RMII input clock */

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_config, &mac_config);
    if (mac == NULL) {
        ESP_LOGE(TAG, "ETH_ERROR: mac_create_failed");
        esp_netif_destroy(s_netif);
        s_netif = NULL;
        return HAL_ERR_IO;
    }

    /* ---- PHY config (generic 802.3 for RTL8201) ---- */
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = (uint32_t)pins.phy_addr;
    phy_config.reset_gpio_num = (pins.reset_pin >= 0) ? pins.reset_pin : -1;
    /* Use auto PHY address detection if board says 0 */

    esp_eth_phy_t *phy = esp_eth_phy_new_rtl8201(&phy_config);
    if (phy == NULL) {
        ESP_LOGE(TAG, "ETH_ERROR: phy_create_failed");
        mac->del(mac);
        esp_netif_destroy(s_netif);
        s_netif = NULL;
        return HAL_ERR_IO;
    }

    /* ---- Install Ethernet driver ---- */
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_err_t ret = esp_eth_driver_install(&eth_config, &s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ETH_ERROR: driver_install_failed (0x%x)", (unsigned)ret);
        phy->del(phy);
        mac->del(mac);
        esp_netif_destroy(s_netif);
        s_netif = NULL;
        s_eth_handle = NULL;
        return HAL_ERR_IO;
    }

    /* ---- Attach netif via glue ---- */
    s_netif_glue = esp_eth_new_netif_glue(s_eth_handle);
    if (s_netif_glue == NULL) {
        ESP_LOGE(TAG, "ETH_ERROR: netif_glue_failed");
        esp_eth_driver_uninstall(s_eth_handle);
        s_eth_handle = NULL;
        esp_netif_destroy(s_netif);
        s_netif = NULL;
        return HAL_ERR_IO;
    }
    ret = esp_netif_attach(s_netif, s_netif_glue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ETH_ERROR: netif_attach_failed (0x%x)", (unsigned)ret);
        esp_eth_del_netif_glue(s_netif_glue);
        s_netif_glue = NULL;
        esp_eth_driver_uninstall(s_eth_handle);
        s_eth_handle = NULL;
        esp_netif_destroy(s_netif);
        s_netif = NULL;
        return HAL_ERR_IO;
    }

    /* ---- Register event handlers ---- */
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_event_handler, NULL);

    /* ---- Start Ethernet driver ---- */
    ret = esp_eth_start(s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ETH_ERROR: start_failed (0x%x)", (unsigned)ret);
        return HAL_ERR_IO;
    }

    /* Enable DHCP on the netif */
    esp_netif_dhcpc_start(s_netif);

    ESP_LOGI(TAG, "ETH: state=starting (waiting for link and DHCP)");
    s_eth_status.initialized = true;

    return HAL_OK;
}

static hal_err_t esp32_eth_deinit_real(void)
{
    if (s_eth_handle != NULL) {
        esp_eth_stop(s_eth_handle);
        esp_eth_driver_uninstall(s_eth_handle);
        s_eth_handle = NULL;
    }
    if (s_netif_glue != NULL) {
        esp_eth_del_netif_glue(s_netif_glue);
        s_netif_glue = NULL;
    }
    if (s_netif != NULL) {
        esp_netif_destroy(s_netif);
        s_netif = NULL;
    }
    esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, eth_event_handler);
    memset(&s_eth_status, 0, sizeof(s_eth_status));
    return HAL_OK;
}

static hal_err_t esp32_eth_get_mac_real(uint8_t mac[6])
{
    if (mac == NULL) return HAL_ERR_INVALID_PARAM;
    esp_read_mac(mac, ESP_MAC_ETH);
    return HAL_OK;
}

static bool esp32_eth_is_connected_real(void)
{
    return s_eth_status.link_up && s_eth_status.got_ip;
}

#else /* UNIT_TEST or NATIVE_TEST — keep stubs */

static hal_err_t esp32_eth_init_real(void)              { return HAL_OK; }
static hal_err_t esp32_eth_deinit_real(void)             { return HAL_OK; }
static hal_err_t esp32_eth_get_mac_real(uint8_t mac[6])  { memset(mac, 0, 6); return HAL_OK; }
static bool     esp32_eth_is_connected_real(void)        { return false; }

#endif /* UNIT_TEST / NATIVE_TEST */

/* ---- Ops table ---- */

static const hal_eth_ops_t s_esp32_eth_ops = {
    .init         = esp32_eth_init_real,
    .deinit       = esp32_eth_deinit_real,
    .get_mac      = esp32_eth_get_mac_real,
    .is_connected = esp32_eth_is_connected_real,
};

const hal_eth_ops_t* hal_eth_esp32_ops(void)
{
    return &s_esp32_eth_ops;
}
