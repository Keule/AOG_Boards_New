/**
 * test_nav_chain — simulation test for the complete NAV data chain.
 *
 * Simulates the full data flow:
 *   Fake UART Primary RX  → gnss_um980 primary
 *   Fake UART Secondary RX → gnss_um980 secondary
 *   Fake TCP (via HAL) → transport_tcp → ntrip_client → RTCM Buffer
 *                                                          → rtcm_router → primary_uart_tx
 *                                                                        → secondary_uart_tx
 *
 * No hardware I/O — only byte_ring_buffer_* and *_service_step() calls.
 */

#include "unity.h"
#include "gnss_um980.h"
#include "ntrip_client.h"
#include "transport_tcp.h"
#include "rtcm_router.h"
#include "byte_ring_buffer.h"
#include "rtcm_passthrough.h"
#include "hal_backend.h"
#include <stdio.h>
#include <string.h>

/* ---- NMEA checksum helper (same as test_gnss_um980) ---- */

static uint8_t nmea_checksum(const char* s)
{
    uint8_t cs = 0;
    for (; *s; s++) cs ^= (uint8_t)*s;
    return cs;
}

static size_t make_nmea(char* buf, size_t buf_size, const char* payload)
{
    uint8_t cs = nmea_checksum(payload);
    return (size_t)snprintf(buf, buf_size, "$%s*%02X\r\n", payload, cs);
}

/* ---- Test data ---- */

#define GGA_PRIMARY_PAYLOAD   "GNGGA,092750.000,5243.1234,N,01322.5678,E,1,12,0.9,34.5,M,47.0,M,,"
#define GGA_SECONDARY_PAYLOAD "GNGGA,092751.000,4812.0000,N,01134.0000,E,2,08,1.5,520.0,M,458.0,M,,"

/* RTCM test bytes (realistic header for a 1003 MSM message) */
static const uint8_t RTCM_BYTES[] = {0xD3, 0x00, 0x13, 0x3E, 0xD7, 0xD3, 0x02, 0x02, 0x98, 0x4E, 0xDE, 0x52, 0x56, 0x62, 0xD8, 0x5C, 0xFE, 0x34, 0x2B, 0x41, 0x97};
#define RTCM_BYTE_COUNT (sizeof(RTCM_BYTES) / sizeof(RTCM_BYTES[0]))

/* ==========================================================================
 * Fake TCP HAL for transport_tcp (shared across sim tests)
 * ========================================================================== */

static uint8_t  s_sim_fake_rx_data[4096];
static size_t  s_sim_fake_rx_len  = 0;
static size_t  s_sim_fake_rx_pos  = 0;
static uint8_t s_sim_fake_tx_written[4096];
static size_t  s_sim_fake_tx_written_len = 0;
static bool    s_sim_fake_connected = false;

static hal_err_t sim_fake_connect(uint32_t ip, uint16_t port)
{
    (void)ip; (void)port;
    s_sim_fake_connected = true;
    return HAL_OK;
}

static hal_err_t sim_fake_disconnect(void) { s_sim_fake_connected = false; return HAL_OK; }

static int sim_fake_recv(uint8_t* buf, size_t max_len)
{
    if (!s_sim_fake_connected) return 0;
    size_t avail = s_sim_fake_rx_len - s_sim_fake_rx_pos;
    if (avail == 0) return 0;
    size_t n = avail > max_len ? max_len : avail;
    memcpy(buf, s_sim_fake_rx_data + s_sim_fake_rx_pos, n);
    s_sim_fake_rx_pos += n;
    return (int)n;
}

static int sim_fake_send(const uint8_t* buf, size_t len)
{
    if (!s_sim_fake_connected) return -1;
    memcpy(s_sim_fake_tx_written + s_sim_fake_tx_written_len, buf, len);
    s_sim_fake_tx_written_len += len;
    return (int)len;
}

static bool sim_fake_is_connected(void) { return s_sim_fake_connected; }

static const transport_tcp_hal_ops_t s_sim_fake_hal = {
    .connect = sim_fake_connect, .disconnect = sim_fake_disconnect,
    .recv = sim_fake_recv, .send = sim_fake_send, .is_connected = sim_fake_is_connected,
};

static void sim_reset_fake(void)
{
    memset(s_sim_fake_rx_data, 0, sizeof(s_sim_fake_rx_data));
    s_sim_fake_rx_len = 0;
    s_sim_fake_rx_pos = 0;
    memset(s_sim_fake_tx_written, 0, sizeof(s_sim_fake_tx_written));
    s_sim_fake_tx_written_len = 0;
    s_sim_fake_connected = false;
}

static void sim_feed_fake_rx(const uint8_t* data, size_t len)
{
    memcpy(s_sim_fake_rx_data + s_sim_fake_rx_len, data, len);
    s_sim_fake_rx_len += len;
}

static void sim_feed_fake_rx_str(const char* str)
{
    sim_feed_fake_rx((const uint8_t*)str, strlen(str));
}

/* ---- Fixtures ---- */

/* GNSS */
static gnss_um980_t primary_gnss;
static gnss_um980_t secondary_gnss;

/* UART RX ring buffers (fake) */
static uint8_t primary_uart_rx_storage[256];
static uint8_t secondary_uart_rx_storage[256];
static byte_ring_buffer_t primary_uart_rx;
static byte_ring_buffer_t secondary_uart_rx;

/* NTRIP + transport_tcp */
static ntrip_client_t ntrip;
static transport_tcp_t ntrip_tcp;
static ntrip_client_config_t ntrip_config;

/* RTCM Router */
static rtcm_router_t router;

/* UART TX ring buffers (fake — simulate UM980 correction input) */
static uint8_t primary_uart_tx_storage[512];
static uint8_t secondary_uart_tx_storage[512];
static byte_ring_buffer_t primary_uart_tx;
static byte_ring_buffer_t secondary_uart_tx;

/* ---- Unity callbacks ---- */

void setUp(void)
{
    sim_reset_fake();

    /* GNSS receivers */
    memset(&primary_gnss,   0, sizeof(primary_gnss));
    memset(&secondary_gnss, 0, sizeof(secondary_gnss));
    gnss_um980_init(&primary_gnss,   0, "gnss_primary");
    gnss_um980_init(&secondary_gnss, 1, "gnss_secondary");

    /* UART RX buffers (fake GNSS data source) */
    memset(primary_uart_rx_storage,   0, sizeof(primary_uart_rx_storage));
    memset(secondary_uart_rx_storage, 0, sizeof(secondary_uart_rx_storage));
    byte_ring_buffer_init(&primary_uart_rx,   primary_uart_rx_storage,   sizeof(primary_uart_rx_storage));
    byte_ring_buffer_init(&secondary_uart_rx, secondary_uart_rx_storage, sizeof(secondary_uart_rx_storage));
    gnss_um980_set_rx_source(&primary_gnss,   &primary_uart_rx);
    gnss_um980_set_rx_source(&secondary_gnss, &secondary_uart_rx);

    /* Transport TCP for NTRIP */
    memset(&ntrip_tcp, 0, sizeof(ntrip_tcp));
    transport_tcp_config_t tcp_cfg = { .remote_ip = 0xC0A80001, .remote_port = 2101 };
    transport_tcp_init(&ntrip_tcp, &tcp_cfg);
    transport_tcp_set_hal_ops(&ntrip_tcp, &s_sim_fake_hal);

    /* NTRIP client */
    memset(&ntrip, 0, sizeof(ntrip));
    memset(&ntrip_config, 0, sizeof(ntrip_config));
    ntrip_client_config_set_defaults(&ntrip_config);
    ntrip_config.mountpoint = "SIM_MOUNT";
    ntrip_client_init(&ntrip);
    ntrip_client_configure(&ntrip, &ntrip_config);
    ntrip_client_set_transport(&ntrip, &ntrip_tcp);

    /* RTCM router */
    memset(&router, 0, sizeof(router));
    rtcm_router_init(&router);

    /* UART TX buffers (fake UM980 correction input) */
    memset(primary_uart_tx_storage,   0, sizeof(primary_uart_tx_storage));
    memset(secondary_uart_tx_storage, 0, sizeof(secondary_uart_tx_storage));
    byte_ring_buffer_init(&primary_uart_tx,   primary_uart_tx_storage,   sizeof(primary_uart_tx_storage));
    byte_ring_buffer_init(&secondary_uart_tx, secondary_uart_tx_storage, sizeof(secondary_uart_tx_storage));
    rtcm_router_add_output(&router, &primary_uart_tx);
    rtcm_router_add_output(&router, &secondary_uart_tx);

    /* Direct architectural wiring: ntrip.rtcm_buffer → rtcm_router source.
     * This mirrors the productive pipeline: transport_tcp → ntrip_client → rtcm_router → transport_uart.
     * No intermediate buffer — the router reads directly from ntrip's RTCM output. */
    rtcm_router_set_source(&router, &ntrip.rtcm_buffer);
}

void tearDown(void) {}

/* ---- Helper: fast-forward ntrip to STREAMING state ---- */

static uint64_t fast_forward_ntrip_to_streaming(void)
{
    ntrip_client_start(&ntrip);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTING, ntrip_client_get_state(&ntrip));

    /* CONNECTING → SEND_REQUEST (transport connects immediately) */
    ntrip_client_service_step((runtime_component_t*)&ntrip, 1000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_SEND_REQUEST, ntrip_client_get_state(&ntrip));

    /* SEND_REQUEST → WAIT_RESPONSE (request written to TX buffer) */
    ntrip_client_service_step((runtime_component_t*)&ntrip, 2000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_WAIT_RESPONSE, ntrip_client_get_state(&ntrip));

    /* Feed NTRIP success response */
    sim_feed_fake_rx_str("ICY 200 OK\r\n\r\n");
    transport_tcp_service_step(&ntrip_tcp.component, 3000);
    ntrip_client_service_step((runtime_component_t*)&ntrip, 3000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_STREAMING, ntrip_client_get_state(&ntrip));

    return 3000;
}

/* ================================================================
 * Teil 2 – Primary RX-Buffer erhält GGA → primary_gnss bekommt Fix
 * ================================================================ */

void test_primary_rx_gga_gives_fix(void)
{
    char sentence[128];
    size_t len = make_nmea(sentence, sizeof(sentence), GGA_PRIMARY_PAYLOAD);

    /* Fake UART: write GGA into primary RX buffer */
    byte_ring_buffer_write(&primary_uart_rx, (const uint8_t*)sentence, len);

    /* service_step reads in 64-byte chunks — call twice for full sentence */
    gnss_um980_service_step((runtime_component_t*)&primary_gnss, 1000000);
    gnss_um980_service_step((runtime_component_t*)&primary_gnss, 2000000);

    TEST_ASSERT_TRUE(gnss_um980_has_fix(&primary_gnss));
    TEST_ASSERT_EQUAL(1, gnss_um980_get_gga(&primary_gnss)->fix_quality);
    TEST_ASSERT_EQUAL(12, gnss_um980_get_gga(&primary_gnss)->num_sats);
}

/* ================================================================
 * Teil 2 – Secondary RX-Buffer erhält GGA → secondary_gnss bekommt Fix
 * ================================================================ */

void test_secondary_rx_gga_gives_fix(void)
{
    char sentence[128];
    size_t len = make_nmea(sentence, sizeof(sentence), GGA_SECONDARY_PAYLOAD);

    byte_ring_buffer_write(&secondary_uart_rx, (const uint8_t*)sentence, len);
    gnss_um980_service_step((runtime_component_t*)&secondary_gnss, 1000000);
    gnss_um980_service_step((runtime_component_t*)&secondary_gnss, 2000000);

    TEST_ASSERT_TRUE(gnss_um980_has_fix(&secondary_gnss));
    TEST_ASSERT_EQUAL(2, gnss_um980_get_gga(&secondary_gnss)->fix_quality);
    TEST_ASSERT_EQUAL(8, gnss_um980_get_gga(&secondary_gnss)->num_sats);
}

/* ================================================================
 * Teil 2 – Beide GNSS-Instanzen beeinflussen sich nicht
 * ================================================================ */

void test_both_gnss_instances_independent(void)
{
    char gga1[128], gga2[128];
    size_t len1 = make_nmea(gga1, sizeof(gga1), GGA_PRIMARY_PAYLOAD);
    size_t len2 = make_nmea(gga2, sizeof(gga2), GGA_SECONDARY_PAYLOAD);

    byte_ring_buffer_write(&primary_uart_rx,   (const uint8_t*)gga1, len1);
    byte_ring_buffer_write(&secondary_uart_rx, (const uint8_t*)gga2, len2);

    /* service_step reads in 64-byte chunks — call twice for full sentence */
    gnss_um980_service_step((runtime_component_t*)&primary_gnss,   1000000);
    gnss_um980_service_step((runtime_component_t*)&primary_gnss,   2000000);
    gnss_um980_service_step((runtime_component_t*)&secondary_gnss, 1000000);
    gnss_um980_service_step((runtime_component_t*)&secondary_gnss, 2000000);

    /* Both have fix */
    TEST_ASSERT_TRUE(gnss_um980_has_fix(&primary_gnss));
    TEST_ASSERT_TRUE(gnss_um980_has_fix(&secondary_gnss));

    /* Different data */
    TEST_ASSERT_NOT_EQUAL(
        gnss_um980_get_gga(&primary_gnss)->latitude,
        gnss_um980_get_gga(&secondary_gnss)->latitude
    );
    TEST_ASSERT_NOT_EQUAL(
        gnss_um980_get_gga(&primary_gnss)->fix_quality,
        gnss_um980_get_gga(&secondary_gnss)->fix_quality
    );

    /* Feeding error to primary doesn't affect secondary */
    gnss_um980_feed(&primary_gnss, (const uint8_t*)"$GNGGA,x*FF\r\n", 12);
    TEST_ASSERT_EQUAL(1, primary_gnss.sentences_error);
    TEST_ASSERT_EQUAL(0, secondary_gnss.sentences_error);
}

/* ================================================================
 * Teil 2 – Fake TCP → transport_tcp → NTRIP → STREAMING → RTCM buffer
 * ================================================================ */

void test_ntrip_connects_and_receives_tcp_data(void)
{
    fast_forward_ntrip_to_streaming();

    /* Push RTCM bytes into fake TCP HAL */
    sim_feed_fake_rx(RTCM_BYTES, RTCM_BYTE_COUNT);

    /* transport_tcp service_step delivers to rx_buffer,
     * then ntrip_client service_step forwards to rtcm_buffer */
    transport_tcp_service_step(&ntrip_tcp.component, 3100000);
    ntrip_client_service_step((runtime_component_t*)&ntrip, 3100000);

    /* ntrip.rtcm_buffer must contain the RTCM data */
    TEST_ASSERT_EQUAL(RTCM_BYTE_COUNT, byte_ring_buffer_available(&ntrip.rtcm_buffer));
    TEST_ASSERT_EQUAL(RTCM_BYTE_COUNT, ntrip_client_rtcm_available(&ntrip));

    /* Verify content via pop */
    uint8_t out[32];
    size_t popped = ntrip_client_pop_rtcm(&ntrip, out, sizeof(out));
    TEST_ASSERT_EQUAL(RTCM_BYTE_COUNT, popped);
    TEST_ASSERT_EQUAL_MEMORY(RTCM_BYTES, out, RTCM_BYTE_COUNT);
}

/* ================================================================
 * Teil 2 – RTCM Router verteilt an beide UART TX Buffer
 * ================================================================ */

void test_rtcm_router_distributes_to_both_uart_tx(void)
{
    /* The router source was already connected in setUp:
     *   rtcm_router_set_source(&router, &ntrip.rtcm_buffer);
     * This is the direct architectural wiring — no intermediate buffer. */

    /* Push NTRIP through the full chain to STREAMING */
    fast_forward_ntrip_to_streaming();

    /* Push RTCM data into TCP → transport_tcp → NTRIP forwards to rtcm_buffer */
    sim_feed_fake_rx(RTCM_BYTES, RTCM_BYTE_COUNT);
    transport_tcp_service_step(&ntrip_tcp.component, 3100000);
    ntrip_client_service_step((runtime_component_t*)&ntrip, 3100000);

    /* Verify: ntrip.rtcm_buffer now contains the RTCM data */
    TEST_ASSERT_EQUAL(RTCM_BYTE_COUNT, byte_ring_buffer_available(&ntrip.rtcm_buffer));

    /* Router service step: reads directly from ntrip.rtcm_buffer,
     * distributes to primary_uart_tx and secondary_uart_tx. */
    rtcm_router_service_step((runtime_component_t*)&router, 3100000);

    /* RTCM data was consumed from ntrip.rtcm_buffer by the router */
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&ntrip.rtcm_buffer));

    /* Both TX buffers should contain the RTCM bytes */
    TEST_ASSERT_EQUAL(RTCM_BYTE_COUNT, byte_ring_buffer_available(&primary_uart_tx));
    TEST_ASSERT_EQUAL(RTCM_BYTE_COUNT, byte_ring_buffer_available(&secondary_uart_tx));

    /* Verify primary TX content matches original RTCM data */
    uint8_t primary_out[32] = {0};
    byte_ring_buffer_read(&primary_uart_tx, primary_out, RTCM_BYTE_COUNT);
    TEST_ASSERT_EQUAL_MEMORY(RTCM_BYTES, primary_out, RTCM_BYTE_COUNT);

    /* Verify secondary TX content matches original RTCM data */
    uint8_t secondary_out[32] = {0};
    byte_ring_buffer_read(&secondary_uart_tx, secondary_out, RTCM_BYTE_COUNT);
    TEST_ASSERT_EQUAL_MEMORY(RTCM_BYTES, secondary_out, RTCM_BYTE_COUNT);

    /* Payloads are identical between primary and secondary */
    TEST_ASSERT_EQUAL_MEMORY(primary_out, secondary_out, RTCM_BYTE_COUNT);

    /* Router stats should reflect the forwarding */
    const rtcm_stats_t* stats = rtcm_router_get_stats(&router);
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_EQUAL(RTCM_BYTE_COUNT, stats->bytes_in);
    TEST_ASSERT_EQUAL(RTCM_BYTE_COUNT * 2, stats->bytes_out);
    TEST_ASSERT_EQUAL(0, stats->bytes_dropped);
}

/* ---- Runner ---- */

int main(void)
{
    UNITY_BEGIN();

    /* GNSS Primary */
    RUN_TEST(test_primary_rx_gga_gives_fix);

    /* GNSS Secondary */
    RUN_TEST(test_secondary_rx_gga_gives_fix);

    /* Independence */
    RUN_TEST(test_both_gnss_instances_independent);

    /* NTRIP chain (now with real transport_tcp) */
    RUN_TEST(test_ntrip_connects_and_receives_tcp_data);

    /* RTCM routing */
    RUN_TEST(test_rtcm_router_distributes_to_both_uart_tx);

    return UNITY_END();
}
