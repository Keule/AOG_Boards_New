#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "app_core.h"
#include "feature_flags.h"
#include "board_profile.h"
#include "runtime.h"
#include "runtime_component.h"

/* ---- Navigation subsystem includes ----
 * Only compiled in when a navigation role is active. */
#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)
#include "transport_uart.h"
#include "transport_udp.h"
#include "transport_tcp.h"
#include "gnss_um980.h"
#include "gnss_dual_heading.h"
#include "ntrip_client.h"
#include "rtcm_router.h"
#include "aog_navigation_app.h"
#endif

static const char* TAG = "APP_CORE";

/* ---- Constants ---- */
#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)
#define UM980_BAUDRATE          921600
#define AOG_UDP_LOCAL_PORT      9999
#define AOG_UDP_REMOTE_PORT     9999
#define NTRIP_DEFAULT_PORT      2101
#endif

/* ---- Static instances (no heap allocation) ---- */
#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)

/* Transport layer */
static transport_uart_t  s_primary_uart;
static transport_uart_t  s_secondary_uart;
static transport_udp_t   s_aog_udp;
static transport_tcp_t   s_ntrip_tcp;

/* GNSS receivers */
static gnss_um980_t          s_primary_gnss;
static gnss_um980_t          s_secondary_gnss;
static gnss_dual_heading_calc_t s_heading;

/* NTRIP + RTCM */
static ntrip_client_t  s_ntrip;
static rtcm_router_t   s_rtcm_router;

/* AOG navigation */
static aog_nav_app_t   s_nav_app;

#endif /* DEVICE_ROLE_NAVIGATION || DEVICE_ROLE_FULL_TEST */

/* ---- Init ---- */

void app_core_init(void)
{
    ESP_LOGI(TAG, "System init");

    unsigned int features = feature_flags_get();

#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)
    if (features & (FEATURE_ROLE_NAVIGATION | FEATURE_ROLE_FULL_TEST)) {
        ESP_LOGI(TAG, "Initializing navigation subsystem");

        /* --- Transport layer --- */

        /* Primary GNSS UART */
        if (board_profile_has_uart(BOARD_UART_GNSS_PRIMARY)) {
            transport_uart_config_t uart_cfg = {
                .port = BOARD_UART_GNSS_PRIMARY,
                .baudrate = UM980_BAUDRATE
            };
            transport_uart_init(&s_primary_uart, &uart_cfg);
        }

        /* Secondary GNSS UART */
        if (board_profile_has_uart(BOARD_UART_GNSS_SECONDARY)) {
            transport_uart_config_t uart_cfg = {
                .port = BOARD_UART_GNSS_SECONDARY,
                .baudrate = UM980_BAUDRATE
            };
            transport_uart_init(&s_secondary_uart, &uart_cfg);
        }

        /* AOG UDP */
        transport_udp_config_t udp_cfg = {
            .local_port = AOG_UDP_LOCAL_PORT,
            .remote_ip = 0xFFFFFFFF,  /* broadcast */
            .remote_port = AOG_UDP_REMOTE_PORT
        };
        transport_udp_init(&s_aog_udp, &udp_cfg);

        /* NTRIP TCP */
        transport_tcp_config_t tcp_cfg = {
            .remote_ip = 0,
            .remote_port = NTRIP_DEFAULT_PORT
        };
        transport_tcp_init(&s_ntrip_tcp, &tcp_cfg);

        /* --- GNSS receivers --- */
        gnss_um980_init(&s_primary_gnss,   0, "GNSS_PRIMARY");
        gnss_um980_init(&s_secondary_gnss, 1, "GNSS_SECONDARY");

        /* Wire GNSS RX sources (transport RX buffer -> GNSS consumer) */
        gnss_um980_set_rx_source(&s_primary_gnss,   &s_primary_uart.rx_buffer);
        gnss_um980_set_rx_source(&s_secondary_gnss, &s_secondary_uart.rx_buffer);

        /* --- Dual heading --- */
        gnss_dual_heading_init(&s_heading);
        gnss_dual_heading_set_sources(&s_heading, &s_primary_gnss, &s_secondary_gnss);

        /* --- NTRIP client --- */
        ntrip_client_init(&s_ntrip);
        ntrip_client_set_tcp_source(&s_ntrip, &s_ntrip_tcp.rx_buffer);

        /* --- RTCM router --- */
        rtcm_router_init(&s_rtcm_router);
        /* Generic source: ntrip_client's RTCM output buffer */
        rtcm_router_set_source(&s_rtcm_router, &s_ntrip.rtcm_buffer);

        /* Wire RTCM outputs -> transport UART TX buffers */
        rtcm_router_add_output(&s_rtcm_router, &s_primary_uart.tx_buffer);
        rtcm_router_add_output(&s_rtcm_router, &s_secondary_uart.tx_buffer);

        /* --- AOG navigation app --- */
        aog_nav_app_init(&s_nav_app);
        /* Position source: primary GNSS snapshot buffer */
        aog_nav_app_set_position_source(&s_nav_app, &s_primary_gnss.position_snapshot);
        /* Heading source: dual heading snapshot buffer */
        aog_nav_app_set_heading_source(&s_nav_app, gnss_dual_heading_get_snapshot(&s_heading));
        /* AOG RX/TX via UDP transport buffers */
        aog_nav_app_set_aog_rx_source(&s_nav_app, &s_aog_udp.rx_buffer);
        aog_nav_app_set_aog_tx_dest(&s_nav_app, &s_aog_udp.tx_buffer);

        /* --- Register components with runtime --- */
        runtime_component_register(&s_primary_uart.component);
        runtime_component_register(&s_secondary_uart.component);
        runtime_component_register(&s_primary_gnss.component);
        runtime_component_register(&s_secondary_gnss.component);
        runtime_component_register(&s_heading.component);
        runtime_component_register(&s_ntrip_tcp.component);
        runtime_component_register(&s_ntrip.component);
        runtime_component_register(&s_rtcm_router.component);
        runtime_component_register(&s_aog_udp.component);
        runtime_component_register(&s_nav_app.component);

        ESP_LOGI(TAG, "Navigation components registered: 10");

        return;
    }
#endif /* DEVICE_ROLE_NAVIGATION || DEVICE_ROLE_FULL_TEST */

#if defined(DEVICE_ROLE_STEERING)
    if (features & FEATURE_ROLE_STEERING) {
        ESP_LOGI(TAG, "Steering role detected (no steering components in v1)");
        return;
    }
#endif

    ESP_LOGI(TAG, "No role-specific components initialized");
}

void app_core_start(void)
{
    runtime_init();
    runtime_start();
}
