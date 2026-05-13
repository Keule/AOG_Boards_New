#include "nav_config.h"

#include <string.h>

#include "esp_log.h"
#include "board_profile.h"
#include "hal_eth.h"
#include "hal_ota.h"
#include "remote_diag.h"
#include "transport_uart.h"
#include "transport_tcp.h"
#include "gnss_um980.h"
#include "nav_ntrip_config.h"

void gnss_rx_task_start(gnss_um980_t* rx, const char* task_name);

static const char* TAG = "NAV_CONFIG";

static transport_uart_t s_gnss_uart1;
static gnss_um980_t    s_gnss_um980_rx1;
static transport_tcp_t s_ntrip_tcp;
static ntrip_client_t  s_ntrip;
static remote_diag_t   s_diag;

static bool s_phase1_done = false;
static bool s_phase2_done = false;

void nav_config_boot_init(void)
{
    if (s_phase1_done) {
        return;
    }

    ESP_LOGI(TAG, "Phase 1 boot init: Ethernet, WebServer, OTA");

    hal_err_t eth_ret = hal_eth_init(hal_eth_esp32_ops());
    if (eth_ret != HAL_OK) {
        ESP_LOGW(TAG, "Ethernet init failed (0x%x)", (unsigned)eth_ret);
    }

    remote_diag_init(&s_diag);
    s_diag.component.service_group = SERVICE_GROUP_DIAGNOSTICS;

    hal_err_t ota_ret = hal_ota_init(hal_ota_esp32_ops());
    if (ota_ret != HAL_OK) {
        ESP_LOGW(TAG, "OTA init failed (0x%x)", (unsigned)ota_ret);
    }

    s_phase1_done = true;
}

void nav_config_boot_phase2(void)
{
    if (s_phase2_done) {
        return;
    }

    ESP_LOGI(TAG, "Phase 2 boot init: GNSS, RX task, NTRIP");

    transport_uart_config_t uart_cfg = {
        .port = BOARD_UART_GNSS_PRIMARY,
        .baudrate = BOARD_GNSS_UART_BAUDRATE,
    };

    hal_err_t uart_ret = transport_uart_init(&s_gnss_uart1, &uart_cfg);
    if (uart_ret != HAL_OK) {
        ESP_LOGW(TAG, "GNSS UART init failed (0x%x)", (unsigned)uart_ret);
    }

    gnss_um980_init(&s_gnss_um980_rx1, 0, "gnss_rx1");
    gnss_um980_set_rx_source(&s_gnss_um980_rx1, &s_gnss_uart1.rx_buffer);
    gnss_rx_task_start(&s_gnss_um980_rx1, "gnss_rx1");

    transport_tcp_config_t tcp_cfg = {
        .remote_port = 2101,
    };
    tcp_cfg.hostname[0] = '\0';

    hal_err_t tcp_ret = transport_tcp_init(&s_ntrip_tcp, &tcp_cfg);
    if (tcp_ret != HAL_OK) {
        ESP_LOGW(TAG, "NTRIP TCP init failed (0x%x)", (unsigned)tcp_ret);
    }

    ntrip_client_init(&s_ntrip);
    ntrip_client_set_transport(&s_ntrip, &s_ntrip_tcp);

    ntrip_client_config_t ntrip_cfg;
    ntrip_config_source_t cfg_src = nav_ntrip_load_config(&ntrip_cfg);
    ntrip_client_configure(&s_ntrip, &ntrip_cfg);
    s_ntrip.config_source = (int)cfg_src;

    if (ntrip_config_is_ready(&ntrip_cfg)) {
        transport_tcp_disconnect(&s_ntrip_tcp);

        transport_tcp_config_t tcp_cfg_real = {
            .remote_port = ntrip_cfg.port,
        };
        strncpy(tcp_cfg_real.hostname, ntrip_cfg.host, TRANSPORT_TCP_HOSTNAME_MAX - 1);
        tcp_cfg_real.hostname[TRANSPORT_TCP_HOSTNAME_MAX - 1] = '\0';

        hal_err_t tcp_real_ret = transport_tcp_init(&s_ntrip_tcp, &tcp_cfg_real);
        if (tcp_real_ret != HAL_OK) {
            ESP_LOGW(TAG, "NTRIP TCP re-init failed (0x%x)", (unsigned)tcp_real_ret);
        }

        ntrip_client_set_transport(&s_ntrip, &s_ntrip_tcp);
        if (!ntrip_client_start(&s_ntrip)) {
            ESP_LOGW(TAG, "NTRIP client start failed");
        }
    } else {
        ESP_LOGW(TAG, "NTRIP client initialised without valid config");
    }

    remote_diag_set_sources(&s_diag,
                            &s_gnss_uart1,
                            NULL,
                            &s_gnss_um980_rx1,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            &s_ntrip,
                            NULL,
                            NULL,
                            NULL);

    s_phase2_done = true;
}
