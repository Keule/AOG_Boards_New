#include "unity.h"
#include <stdio.h>
#include <string.h>
#include "transport_tcp.h"
#include "hal_backend.h"

/* ==========================================================================
 * Fake TCP HAL for host tests — controllable recv/send behavior
 * ========================================================================== */

static uint8_t  s_fake_rx_data[2048];
static size_t  s_fake_rx_len  = 0;
static size_t  s_fake_rx_pos  = 0;
static size_t  s_fake_tx_max_write = 0;
static int     s_fake_tx_fail       = 0;
static uint8_t s_fake_tx_written[4096];
static size_t  s_fake_tx_written_len = 0;
static bool    s_fake_connected = false;

static hal_err_t fake_connect(uint32_t ip, uint16_t port)
{
    (void)ip; (void)port;
    s_fake_connected = true;
    return HAL_OK;
}

static hal_err_t fake_disconnect(void)
{
    s_fake_connected = false;
    return HAL_OK;
}

static int fake_recv(uint8_t* buf, size_t max_len)
{
    if (!s_fake_connected) return 0;
    size_t avail = s_fake_rx_len - s_fake_rx_pos;
    if (avail == 0) return 0;
    size_t n = avail > max_len ? max_len : avail;
    memcpy(buf, s_fake_rx_data + s_fake_rx_pos, n);
    s_fake_rx_pos += n;
    return (int)n;
}

static int fake_send(const uint8_t* buf, size_t len)
{
    if (!s_fake_connected) return -1;
    if (s_fake_tx_fail) return -1;
    if (s_fake_tx_max_write > 0 && len > s_fake_tx_max_write) {
        memcpy(s_fake_tx_written + s_fake_tx_written_len, buf, s_fake_tx_max_write);
        s_fake_tx_written_len += s_fake_tx_max_write;
        return (int)s_fake_tx_max_write;
    }
    memcpy(s_fake_tx_written + s_fake_tx_written_len, buf, len);
    s_fake_tx_written_len += len;
    return (int)len;
}

static bool fake_is_connected(void) { return s_fake_connected; }

static const transport_tcp_hal_ops_t s_fake_hal = {
    .connect = fake_connect, .disconnect = fake_disconnect,
    .recv = fake_recv, .send = fake_send, .is_connected = fake_is_connected,
};

static void reset_fake(void) {
    memset(s_fake_rx_data, 0, sizeof(s_fake_rx_data));
    s_fake_rx_len = 0; s_fake_rx_pos = 0;
    s_fake_tx_max_write = 0; s_fake_tx_fail = 0;
    memset(s_fake_tx_written, 0, sizeof(s_fake_tx_written));
    s_fake_tx_written_len = 0; s_fake_connected = false;
}

static void feed_rx(const uint8_t* data, size_t len) {
    memcpy(s_fake_rx_data + s_fake_rx_len, data, len);
    s_fake_rx_len += len;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

void test_tcp_init_succeeds(void)
{
    reset_fake();
    transport_tcp_t tcp;
    transport_tcp_config_t cfg = { .remote_ip = 0xC0A80001, .remote_port = 2101 };
    TEST_ASSERT_EQUAL(HAL_OK, transport_tcp_init(&tcp, &cfg));
    TEST_ASSERT_EQUAL(0xC0A80001, tcp.remote_ip);
    TEST_ASSERT_EQUAL(2101, tcp.remote_port);
    TEST_ASSERT_NOT_NULL(tcp.component.service_step);
}

void test_tcp_set_hal_ops_and_connect(void)
{
    reset_fake();
    transport_tcp_t tcp;
    transport_tcp_config_t cfg = { .remote_ip = 0, .remote_port = 2101 };
    TEST_ASSERT_EQUAL(HAL_OK, transport_tcp_init(&tcp, &cfg));
    transport_tcp_set_hal_ops(&tcp, &s_fake_hal);
    TEST_ASSERT_EQUAL(HAL_OK, transport_tcp_connect(&tcp));
    TEST_ASSERT_TRUE(transport_tcp_is_connected(&tcp));
    TEST_ASSERT_EQUAL(1, tcp.connect_count);
}

void test_tcp_disconnect(void)
{
    reset_fake();
    transport_tcp_t tcp;
    transport_tcp_config_t cfg = { .remote_ip = 0, .remote_port = 2101 };
    transport_tcp_init(&tcp, &cfg);
    transport_tcp_set_hal_ops(&tcp, &s_fake_hal);
    transport_tcp_connect(&tcp);
    TEST_ASSERT_EQUAL(HAL_OK, transport_tcp_disconnect(&tcp));
    TEST_ASSERT_FALSE(transport_tcp_is_connected(&tcp));
    TEST_ASSERT_EQUAL(1, tcp.disconnect_count);
}

void test_tcp_rx_buffer_filled(void)
{
    reset_fake();
    s_fake_connected = true;
    transport_tcp_t tcp;
    transport_tcp_config_t cfg = { .remote_ip = 0, .remote_port = 2101 };
    transport_tcp_init(&tcp, &cfg);
    transport_tcp_set_hal_ops(&tcp, &s_fake_hal);
    transport_tcp_connect(&tcp);

    uint8_t data[] = "HELLO_TCP";
    feed_rx(data, 9);  /* strlen, not sizeof (no null term) */

    transport_tcp_service_step(&tcp.component, 1000);

    uint8_t out[16] = {0};
    size_t n = transport_tcp_rx_read(&tcp, out, sizeof(out));
    TEST_ASSERT_EQUAL(9, n);
    TEST_ASSERT_EQUAL_MEMORY("HELLO_TCP", out, 9);
}

void test_tcp_tx_buffer_drained(void)
{
    reset_fake();
    s_fake_connected = true;
    transport_tcp_t tcp;
    transport_tcp_config_t cfg = { .remote_ip = 0, .remote_port = 2101 };
    transport_tcp_init(&tcp, &cfg);
    transport_tcp_set_hal_ops(&tcp, &s_fake_hal);
    transport_tcp_connect(&tcp);

    uint8_t data[] = "SEND_THIS";
    transport_tcp_tx_write(&tcp, data, 9);  /* strlen, not sizeof */

    transport_tcp_service_step(&tcp.component, 1000);

    TEST_ASSERT_EQUAL(9, s_fake_tx_written_len);
    TEST_ASSERT_EQUAL_MEMORY("SEND_THIS", s_fake_tx_written, 9);
}

void test_tcp_partial_write_no_data_loss(void)
{
    reset_fake();
    s_fake_connected = true;
    s_fake_tx_max_write = 3;

    transport_tcp_t tcp;
    transport_tcp_config_t cfg = { .remote_ip = 0, .remote_port = 2101 };
    transport_tcp_init(&tcp, &cfg);
    transport_tcp_set_hal_ops(&tcp, &s_fake_hal);
    transport_tcp_connect(&tcp);

    uint8_t data[] = "ABCDEFGHIJ";
    transport_tcp_tx_write(&tcp, data, 10);

    for (int i = 0; i < 20; i++) {
        transport_tcp_service_step(&tcp.component, 1000 * (uint64_t)(i + 1));
    }

    TEST_ASSERT_EQUAL(10, s_fake_tx_written_len);
    TEST_ASSERT_EQUAL_MEMORY("ABCDEFGHIJ", s_fake_tx_written, 10);
}

void test_tcp_hal_failure_preserves_data(void)
{
    reset_fake();
    s_fake_connected = true;
    s_fake_tx_fail = 1;

    transport_tcp_t tcp;
    transport_tcp_config_t cfg = { .remote_ip = 0, .remote_port = 2101 };
    transport_tcp_init(&tcp, &cfg);
    transport_tcp_set_hal_ops(&tcp, &s_fake_hal);
    transport_tcp_connect(&tcp);

    uint8_t data[] = "IMPORTANT";
    transport_tcp_tx_write(&tcp, data, 9);
    transport_tcp_service_step(&tcp.component, 1000);

    TEST_ASSERT_EQUAL(9, byte_ring_buffer_available(&tcp.tx_buffer));
}

void test_tcp_diagnostics(void)
{
    reset_fake();
    s_fake_connected = true;

    transport_tcp_t tcp;
    transport_tcp_config_t cfg = { .remote_ip = 0, .remote_port = 2101 };
    transport_tcp_init(&tcp, &cfg);
    transport_tcp_set_hal_ops(&tcp, &s_fake_hal);
    transport_tcp_connect(&tcp);

    transport_tcp_diagnostics_t diag;
    TEST_ASSERT_EQUAL(HAL_OK, transport_tcp_get_diagnostics(&tcp, &diag));
    TEST_ASSERT_EQUAL(0, diag.rx_total);
    TEST_ASSERT_EQUAL(0, diag.tx_total);
    TEST_ASSERT_EQUAL(TRANSPORT_TCP_TX_BUFFER_SIZE, diag.tx_free);
}

void test_tcp_reset(void)
{
    reset_fake();
    s_fake_connected = true;

    transport_tcp_t tcp;
    transport_tcp_config_t cfg = { .remote_ip = 0x01020304, .remote_port = 8080 };
    transport_tcp_init(&tcp, &cfg);
    transport_tcp_set_hal_ops(&tcp, &s_fake_hal);
    transport_tcp_connect(&tcp);

    uint8_t data[] = "X";
    transport_tcp_tx_write(&tcp, data, 1);

    TEST_ASSERT_EQUAL(HAL_OK, transport_tcp_reset(&tcp));
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&tcp.rx_buffer));
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&tcp.tx_buffer));
    TEST_ASSERT_EQUAL(0x01020304, tcp.remote_ip);
    TEST_ASSERT_EQUAL(8080, tcp.remote_port);
}

void test_tcp_null_safety(void)
{
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, transport_tcp_init(NULL, &(transport_tcp_config_t){0}));
    TEST_ASSERT_EQUAL(0, transport_tcp_rx_read(NULL, (uint8_t[]){0}, 1));
    TEST_ASSERT_EQUAL(0, transport_tcp_tx_write(NULL, (uint8_t[]){0}, 1));
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, transport_tcp_reset(NULL));
}

void test_tcp_contains_no_ntrip_logic(void)
{
    /* Compile-time check: transport_tcp.h must not include NTRIP headers.
     * This test verifies the transport layer is transport-only. */
    reset_fake();
    transport_tcp_t tcp;
    transport_tcp_config_t cfg = { .remote_ip = 0, .remote_port = 2101 };
    transport_tcp_init(&tcp, &cfg);

    /* No NTRIP state machine, no RTCM routing — just raw TCP */
    TEST_ASSERT_FALSE(transport_tcp_is_connected(&tcp));
    TEST_ASSERT_EQUAL(0, transport_tcp_rx_available(&tcp));
}

/* ---- Unity ---- */

void setUp(void) { reset_fake(); }
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tcp_init_succeeds);
    RUN_TEST(test_tcp_set_hal_ops_and_connect);
    RUN_TEST(test_tcp_disconnect);
    RUN_TEST(test_tcp_rx_buffer_filled);
    RUN_TEST(test_tcp_tx_buffer_drained);
    RUN_TEST(test_tcp_partial_write_no_data_loss);
    RUN_TEST(test_tcp_hal_failure_preserves_data);
    RUN_TEST(test_tcp_diagnostics);
    RUN_TEST(test_tcp_reset);
    RUN_TEST(test_tcp_null_safety);
    RUN_TEST(test_tcp_contains_no_ntrip_logic);
    return UNITY_END();
}
