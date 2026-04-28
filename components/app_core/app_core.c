#include "app_core.h"

#include "feature_flags.h"
#include "runtime.h"
#include "byte_ring_buffer.h"
#include "gnss_um980.h"
#include "gnss_dual_heading.h"
#include "ntrip_client.h"
#include "rtcm_router.h"
#include "aog_navigation_app.h"
#include "transport_uart.h"
#include "transport_udp.h"
#include "transport_tcp.h"

#define UM980_RX_BUFFER_CAPACITY 1024U
#define UM980_TX_BUFFER_CAPACITY 1024U
#define RTCM_INPUT_BUFFER_CAPACITY 1024U

static uint8_t s_um980_primary_rx_storage[UM980_RX_BUFFER_CAPACITY];
static uint8_t s_um980_secondary_rx_storage[UM980_RX_BUFFER_CAPACITY];
static uint8_t s_um980_primary_tx_storage[UM980_TX_BUFFER_CAPACITY];
static uint8_t s_um980_secondary_tx_storage[UM980_TX_BUFFER_CAPACITY];
static uint8_t s_rtcm_input_storage[RTCM_INPUT_BUFFER_CAPACITY];

static byte_ring_buffer_t s_um980_primary_rx_buffer;
static byte_ring_buffer_t s_um980_secondary_rx_buffer;
static byte_ring_buffer_t s_um980_primary_tx_buffer;
static byte_ring_buffer_t s_um980_secondary_tx_buffer;
static byte_ring_buffer_t s_rtcm_input_buffer;

static gnss_um980_t s_um980_primary;
static gnss_um980_t s_um980_secondary;
static gnss_dual_heading_t s_dual_heading;
static ntrip_client_t s_ntrip_client;
static rtcm_router_t s_rtcm_router;
static aog_navigation_app_t s_aog_navigation_app;

static transport_uart_channel_t s_uart_channels[2];
static transport_udp_t s_transport_udp;
static transport_tcp_t s_transport_tcp;
static unsigned int s_features = 0;

static bool navigation_role_enabled(void)
{
    return ((s_features & FEATURE_ROLE_NAVIGATION) != 0U) || ((s_features & FEATURE_ROLE_FULL_TEST) != 0U);
}

static void app_core_register_navigation_components(void)
{
    runtime_register(gnss_um980_component(&s_um980_primary));
    runtime_register(gnss_um980_component(&s_um980_secondary));
    runtime_register(gnss_dual_heading_component(&s_dual_heading));
    runtime_register(rtcm_router_component(&s_rtcm_router));
    runtime_register(aog_navigation_app_component(&s_aog_navigation_app));
}

    s_features = feature_flags_get();

    runtime_init();

    byte_ring_buffer_init(&s_um980_primary_rx_buffer, s_um980_primary_rx_storage, sizeof(s_um980_primary_rx_storage));
    byte_ring_buffer_init(&s_um980_secondary_rx_buffer, s_um980_secondary_rx_storage, sizeof(s_um980_secondary_rx_storage));
    byte_ring_buffer_init(&s_um980_primary_tx_buffer, s_um980_primary_tx_storage, sizeof(s_um980_primary_tx_storage));
    byte_ring_buffer_init(&s_um980_secondary_tx_buffer, s_um980_secondary_tx_storage, sizeof(s_um980_secondary_tx_storage));
    byte_ring_buffer_init(&s_rtcm_input_buffer, s_rtcm_input_storage, sizeof(s_rtcm_input_storage));

    gnss_um980_init(&s_um980_primary, GNSS_UM980_PRIMARY, &s_um980_primary_rx_buffer, &s_um980_primary_tx_buffer);
    gnss_um980_init(&s_um980_secondary, GNSS_UM980_SECONDARY, &s_um980_secondary_rx_buffer, &s_um980_secondary_tx_buffer);
    gnss_dual_heading_init(&s_dual_heading, &s_um980_primary, &s_um980_secondary);

    ntrip_client_init(&s_ntrip_client);
    ntrip_client_request_connect(&s_ntrip_client);

    rtcm_router_init(&s_rtcm_router, &s_rtcm_input_buffer);
    rtcm_router_register_output(&s_rtcm_router, &s_um980_primary_tx_buffer);
    rtcm_router_register_output(&s_rtcm_router, &s_um980_secondary_tx_buffer);

    aog_navigation_app_init(&s_aog_navigation_app, &s_um980_primary, &s_dual_heading);

    transport_uart_bind(&s_uart_channels[0], &s_um980_primary_rx_buffer, &s_um980_primary_tx_buffer);
    transport_uart_bind(&s_uart_channels[1], &s_um980_secondary_rx_buffer, &s_um980_secondary_tx_buffer);
    transport_udp_init(&s_transport_udp);
    transport_tcp_init(&s_transport_tcp);

    if (navigation_role_enabled()) {
        app_core_register_navigation_components();
    }

    runtime_start();
{
    ESP_LOGI(TAG, "System init");
}

void app_core_start(void)
{
    runtime_init();
    runtime_start();
}
