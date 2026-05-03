/*
 * NAV-RTCM-001 End-to-End Navigation Chain Simulation Test
 *
 * Simulates the full RTCM routing chain without any physical hardware:
 *
 *   NTRIP Client (CONNECTED) → rtcm_buffer
 *   → RTCM Router → primary_uart.tx_buffer + secondary_uart.tx_buffer
 *   → transport_uart service step → HAL UART TX
 *
 * This test verifies the complete data flow using only ring buffers and stubs.
 */

#include "unity.h"
#include "ntrip_client.h"
#include "rtcm_router.h"
#include "transport_uart.h"
#include "transport_tcp.h"
#include "hal_uart.h"
#include "byte_ring_buffer.h"
#include <string.h>

/* ---- HAL stub: captures TX bytes ---- */

static uint8_t  s_hal_tx_primary[1024];
static size_t   s_hal_tx_primary_len = 0;
static uint8_t  s_hal_tx_secondary[1024];
static size_t   s_hal_tx_secondary_len = 0;

static int sim_hal_read(board_uart_port_t port, uint8_t* buf, size_t max_len)
{
    (void)port; (void)buf; (void)max_len;
    return 0;
}

static int sim_hal_write(board_uart_port_t port, const uint8_t* buf, size_t len)
{
    if (port == BOARD_UART_GNSS_PRIMARY) {
        size_t avail = sizeof(s_hal_tx_primary) - s_hal_tx_primary_len;
        size_t n = len > avail ? avail : len;
        memcpy(s_hal_tx_primary + s_hal_tx_primary_len, buf, n);
        s_hal_tx_primary_len += n;
        return (int)n;
    }
    if (port == BOARD_UART_GNSS_SECONDARY) {
        size_t avail = sizeof(s_hal_tx_secondary) - s_hal_tx_secondary_len;
        size_t n = len > avail ? avail : len;
        memcpy(s_hal_tx_secondary + s_hal_tx_secondary_len, buf, n);
        s_hal_tx_secondary_len += n;
        return (int)n;
    }
    return -1;
}

static hal_err_t sim_hal_init(board_uart_port_t port, const hal_uart_config_t* config)
{
    (void)port; (void)config;
    return HAL_OK;
}

static hal_err_t sim_hal_deinit(board_uart_port_t port)
{
    (void)port;
    return HAL_OK;
}

static hal_err_t sim_hal_flush(board_uart_port_t port)
{
    (void)port;
    return HAL_OK;
}

static hal_err_t sim_hal_reset(board_uart_port_t port)
{
    (void)port;
    return HAL_OK;
}

static int sim_hal_available(board_uart_port_t port)
{
    (void)port;
    return 0;
}

static const hal_uart_ops_t s_sim_hal_ops = {
    .init      = sim_hal_init,
    .deinit    = sim_hal_deinit,
    .read      = sim_hal_read,
    .write     = sim_hal_write,
    .flush     = sim_hal_flush,
    .reset     = sim_hal_reset,
    .available = sim_hal_available,
};

/* ---- Test fixtures ---- */

static ntrip_client_t     s_ntrip;
static transport_tcp_t    s_tcp;
static rtcm_router_t      s_router;
static transport_uart_t   s_primary_uart;
static transport_uart_t   s_secondary_uart;

static void reset_hal_captures(void)
{
    memset(s_hal_tx_primary, 0, sizeof(s_hal_tx_primary));
    s_hal_tx_primary_len = 0;
    memset(s_hal_tx_secondary, 0, sizeof(s_hal_tx_secondary));
    s_hal_tx_secondary_len = 0;
}

static void configure_ntrip(ntrip_client_t* client)
{
    ntrip_client_config_t cfg = NTRIP_CLIENT_CONFIG_DEFAULT();
    strncpy(cfg.host, "ntrip.example.com", sizeof(cfg.host) - 1);
    strncpy(cfg.mountpoint, "RTCM3_GGB", sizeof(cfg.mountpoint) - 1);
    strncpy(cfg.username, "user", sizeof(cfg.username) - 1);
    strncpy(cfg.password, "pass", sizeof(cfg.password) - 1);
    cfg.port = 2101;
    cfg.timeout_ms = 5000;
    cfg.reconnect_backoff_ms = 2000;
    ntrip_client_configure(client, &cfg);
}

static void inject_http_200(transport_tcp_t* tcp)
{
    const char* resp = "HTTP/1.1 200 OK\r\n\r\n";
    byte_ring_buffer_write(&tcp->rx_buffer, (const uint8_t*)resp, strlen(resp));
}

void setUp(void)
{
    reset_hal_captures();
    hal_uart_init(&s_sim_hal_ops);

    memset(&s_tcp, 0, sizeof(s_tcp));
    transport_tcp_config_t tcp_cfg = { .remote_port = 2101 };
    strncpy(tcp_cfg.hostname, "1.2.3.4", sizeof(tcp_cfg.hostname) - 1);
    transport_tcp_init(&s_tcp, &tcp_cfg);

    memset(&s_ntrip, 0, sizeof(s_ntrip));
    ntrip_client_init(&s_ntrip);

    memset(&s_router, 0, sizeof(s_router));
    rtcm_router_init(&s_router);

    memset(&s_primary_uart, 0, sizeof(s_primary_uart));
    memset(&s_secondary_uart, 0, sizeof(s_secondary_uart));
}

void tearDown(void)
{
    hal_uart_deinit();
}

/* ---- Test: Full chain wiring ---- */

void test_chain_init_and_wiring(void)
{
    /* Init transport UARTs */
    transport_uart_config_t pri_cfg = { .port = BOARD_UART_GNSS_PRIMARY, .baudrate = 921600 };
    transport_uart_config_t sec_cfg = { .port = BOARD_UART_GNSS_SECONDARY, .baudrate = 921600 };
    TEST_ASSERT_EQUAL(HAL_OK, transport_uart_init(&s_primary_uart, &pri_cfg));
    TEST_ASSERT_EQUAL(HAL_OK, transport_uart_init(&s_secondary_uart, &sec_cfg));

    /* Wire NTRIP → Router → UARTs (same as app_core.c) */
    rtcm_router_set_source(&s_router, &s_ntrip.rtcm_buffer);
    rtcm_router_add_output(&s_router, &s_primary_uart.tx_buffer);
    rtcm_router_add_output(&s_router, &s_secondary_uart.tx_buffer);

    TEST_ASSERT_EQUAL_PTR(&s_ntrip.rtcm_buffer, s_router.rtcm_source);
    TEST_ASSERT_EQUAL(2, s_router.output_count);
}

/* ---- Test: NTRIP → RTCM Buffer → Router → UART TX Buffers ---- */

void test_rtcm_flows_through_full_chain(void)
{
    /* Init and wire */
    transport_uart_config_t pri_cfg = { .port = BOARD_UART_GNSS_PRIMARY, .baudrate = 921600 };
    transport_uart_config_t sec_cfg = { .port = BOARD_UART_GNSS_SECONDARY, .baudrate = 921600 };
    transport_uart_init(&s_primary_uart, &pri_cfg);
    transport_uart_init(&s_secondary_uart, &sec_cfg);

    rtcm_router_set_source(&s_router, &s_ntrip.rtcm_buffer);
    rtcm_router_add_output(&s_router, &s_primary_uart.tx_buffer);
    rtcm_router_add_output(&s_router, &s_secondary_uart.tx_buffer);

    /* Connect NTRIP client to TCP and start */
    ntrip_client_set_transport(&s_ntrip, &s_tcp);
    configure_ntrip(&s_ntrip);
    ntrip_client_start(&s_ntrip);

    /* Fast-forward to CONNECTED state */
    transport_tcp_connect(&s_tcp);
    ntrip_client_service_step((runtime_component_t*)&s_ntrip, 1000);
    inject_http_200(&s_tcp);
    ntrip_client_service_step((runtime_component_t*)&s_ntrip, 2000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTED, ntrip_client_get_state(&s_ntrip));

    /* Inject RTCM data into TCP rx (simulating NTRIP caster) */
    uint8_t rtcm_data[] = {0xD3, 0x00, 0x13, 0x3E, 0xD7, 0x00, 0x01, 0x02, 0x03};
    byte_ring_buffer_write(&s_tcp.rx_buffer, rtcm_data, sizeof(rtcm_data));

    /* Step 1: NTRIP service — reads from TCP, writes to rtcm_buffer */
    ntrip_client_service_step((runtime_component_t*)&s_ntrip, 3000);
    TEST_ASSERT_EQUAL(sizeof(rtcm_data), ntrip_client_rtcm_available(&s_ntrip));

    /* Step 2: Router service — reads from rtcm_buffer, writes to both UART TX */
    rtcm_router_service_step((runtime_component_t*)&s_router, 3000);
    TEST_ASSERT_EQUAL(sizeof(rtcm_data), byte_ring_buffer_available(&s_primary_uart.tx_buffer));
    TEST_ASSERT_EQUAL(sizeof(rtcm_data), byte_ring_buffer_available(&s_secondary_uart.tx_buffer));

    /* Source should be drained */
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&s_ntrip.rtcm_buffer));

    /* Step 3: UART service steps — drain TX buffers to HAL */
    transport_uart_service_step((runtime_component_t*)&s_primary_uart, 3000);
    transport_uart_service_step((runtime_component_t*)&s_secondary_uart, 3000);

    /* Both HAL captures should have the RTCM data */
    TEST_ASSERT_EQUAL(sizeof(rtcm_data), s_hal_tx_primary_len);
    TEST_ASSERT_EQUAL(sizeof(rtcm_data), s_hal_tx_secondary_len);
    TEST_ASSERT_EQUAL_MEMORY(rtcm_data, s_hal_tx_primary, sizeof(rtcm_data));
    TEST_ASSERT_EQUAL_MEMORY(rtcm_data, s_hal_tx_secondary, sizeof(rtcm_data));
}

/* ---- Test: Multiple RTCM messages through the chain ---- */

void test_multiple_rtcm_messages_routed(void)
{
    transport_uart_config_t pri_cfg = { .port = BOARD_UART_GNSS_PRIMARY, .baudrate = 921600 };
    transport_uart_config_t sec_cfg = { .port = BOARD_UART_GNSS_SECONDARY, .baudrate = 921600 };
    transport_uart_init(&s_primary_uart, &pri_cfg);
    transport_uart_init(&s_secondary_uart, &sec_cfg);

    rtcm_router_set_source(&s_router, &s_ntrip.rtcm_buffer);
    rtcm_router_add_output(&s_router, &s_primary_uart.tx_buffer);
    rtcm_router_add_output(&s_router, &s_secondary_uart.tx_buffer);

    ntrip_client_set_transport(&s_ntrip, &s_tcp);
    configure_ntrip(&s_ntrip);
    ntrip_client_start(&s_ntrip);
    transport_tcp_connect(&s_tcp);
    ntrip_client_service_step((runtime_component_t*)&s_ntrip, 1000);
    inject_http_200(&s_tcp);
    ntrip_client_service_step((runtime_component_t*)&s_ntrip, 2000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTED, ntrip_client_get_state(&s_ntrip));

    /* Inject 3 RTCM messages sequentially */
    uint8_t msg1[] = {0xD3, 0x00, 0x03, 0x01, 0x02};
    uint8_t msg2[] = {0xD3, 0x00, 0x04, 0xAA, 0xBB};
    uint8_t msg3[] = {0xD3, 0x00, 0x02, 0xCC};

    /* Message 1 */
    byte_ring_buffer_write(&s_tcp.rx_buffer, msg1, sizeof(msg1));
    ntrip_client_service_step((runtime_component_t*)&s_ntrip, 3000);
    rtcm_router_service_step((runtime_component_t*)&s_router, 3000);

    /* Message 2 */
    byte_ring_buffer_write(&s_tcp.rx_buffer, msg2, sizeof(msg2));
    ntrip_client_service_step((runtime_component_t*)&s_ntrip, 4000);
    rtcm_router_service_step((runtime_component_t*)&s_router, 4000);

    /* Message 3 */
    byte_ring_buffer_write(&s_tcp.rx_buffer, msg3, sizeof(msg3));
    ntrip_client_service_step((runtime_component_t*)&s_ntrip, 5000);
    rtcm_router_service_step((runtime_component_t*)&s_router, 5000);

    /* Stats check */
    const rtcm_stats_t* stats = rtcm_router_get_stats(&s_router);
    TEST_ASSERT_NOT_NULL(stats);
    size_t total = sizeof(msg1) + sizeof(msg2) + sizeof(msg3);
    TEST_ASSERT_EQUAL(total, stats->bytes_in);
    TEST_ASSERT_EQUAL(total * 2, stats->bytes_out);  /* 2 outputs */
    TEST_ASSERT_EQUAL(0, stats->bytes_dropped);

    /* Drain to HAL */
    transport_uart_service_step((runtime_component_t*)&s_primary_uart, 5000);
    transport_uart_service_step((runtime_component_t*)&s_secondary_uart, 5000);

    /* Both UARTs should have all bytes */
    TEST_ASSERT_EQUAL(total, s_hal_tx_primary_len);
    TEST_ASSERT_EQUAL(total, s_hal_tx_secondary_len);

    /* Verify content: first bytes should be msg1 */
    TEST_ASSERT_EQUAL_MEMORY(msg1, s_hal_tx_primary, sizeof(msg1));
    TEST_ASSERT_EQUAL_MEMORY(msg1, s_hal_tx_secondary, sizeof(msg1));

    /* TX stats on primary UART */
    const transport_uart_stats_t* tx_stats = transport_uart_get_stats(&s_primary_uart);
    TEST_ASSERT_EQUAL(total, tx_stats->tx_bytes_out);
}

/* ---- Test: Service chain order is important ---- */

void test_service_chain_order_ntrip_router_uart(void)
{
    /* This test verifies the correct calling order:
     * 1. ntrip_client_service_step()  → fills rtcm_buffer
     * 2. rtcm_router_service_step()   → drains rtcm_buffer, fills TX buffers
     * 3. transport_uart_service_step() → drains TX buffers, writes to HAL
     */

    transport_uart_config_t pri_cfg = { .port = BOARD_UART_GNSS_PRIMARY, .baudrate = 921600 };
    transport_uart_init(&s_primary_uart, &pri_cfg);

    rtcm_router_set_source(&s_router, &s_ntrip.rtcm_buffer);
    rtcm_router_add_output(&s_router, &s_primary_uart.tx_buffer);

    ntrip_client_set_transport(&s_ntrip, &s_tcp);
    configure_ntrip(&s_ntrip);
    ntrip_client_start(&s_ntrip);
    transport_tcp_connect(&s_tcp);
    ntrip_client_service_step((runtime_component_t*)&s_ntrip, 1000);
    inject_http_200(&s_tcp);
    ntrip_client_service_step((runtime_component_t*)&s_ntrip, 2000);

    uint8_t rtcm[] = {0xD3, 0x00, 0x13, 0x3E, 0xD7};
    byte_ring_buffer_write(&s_tcp.rx_buffer, rtcm, sizeof(rtcm));

    /* Step 1 only (NTRIP): RTCM in ntrip buffer, not yet routed */
    ntrip_client_service_step((runtime_component_t*)&s_ntrip, 3000);
    TEST_ASSERT_TRUE(ntrip_client_rtcm_available(&s_ntrip) > 0);
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&s_primary_uart.tx_buffer));

    /* Step 2 (Router): RTCM routed to UART TX buffer */
    rtcm_router_service_step((runtime_component_t*)&s_router, 3000);
    TEST_ASSERT_EQUAL(0, ntrip_client_rtcm_available(&s_ntrip));
    TEST_ASSERT_TRUE(byte_ring_buffer_available(&s_primary_uart.tx_buffer) > 0);

    /* Step 3 (UART): TX buffer drained to HAL */
    transport_uart_service_step((runtime_component_t*)&s_primary_uart, 3000);
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&s_primary_uart.tx_buffer));
    TEST_ASSERT_EQUAL(sizeof(rtcm), s_hal_tx_primary_len);
}

/* ---- Test: Router does NOT touch HAL ---- */

void test_router_no_hal_interaction(void)
{
    /* Router should work without any UART/HAL initialization */
    rtcm_router_set_source(&s_router, &s_ntrip.rtcm_buffer);

    /* Create raw ring buffers (not transport_uart, just byte_ring_buffer_t) */
    uint8_t out1_storage[128];
    uint8_t out2_storage[128];
    byte_ring_buffer_t out1, out2;
    byte_ring_buffer_init(&out1, out1_storage, sizeof(out1_storage));
    byte_ring_buffer_init(&out2, out2_storage, sizeof(out2_storage));

    rtcm_router_add_output(&s_router, &out1);
    rtcm_router_add_output(&s_router, &out2);

    /* Manually inject RTCM data into ntrip buffer */
    uint8_t rtcm[] = {0xD3, 0x00, 0x02, 0xCC};
    byte_ring_buffer_write(&s_ntrip.rtcm_buffer, rtcm, sizeof(rtcm));

    /* Router service step — pure buffer ops, no HAL */
    rtcm_router_service_step((runtime_component_t*)&s_router, 1000);

    /* Data should be in both outputs */
    TEST_ASSERT_EQUAL(sizeof(rtcm), byte_ring_buffer_available(&out1));
    TEST_ASSERT_EQUAL(sizeof(rtcm), byte_ring_buffer_available(&out2));

    /* HAL captures should still be empty */
    TEST_ASSERT_EQUAL(0, s_hal_tx_primary_len);
    TEST_ASSERT_EQUAL(0, s_hal_tx_secondary_len);
}

/* ---- Main ---- */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_chain_init_and_wiring);
    RUN_TEST(test_rtcm_flows_through_full_chain);
    RUN_TEST(test_multiple_rtcm_messages_routed);
    RUN_TEST(test_service_chain_order_ntrip_router_uart);
    RUN_TEST(test_router_no_hal_interaction);

    UNITY_END();
}
