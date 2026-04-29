#include "unity.h"
#include <stdio.h>
#include <string.h>
#include "transport_udp.h"
#include "hal_backend.h"

/* ==========================================================================
 * Fake UDP HAL for host tests — controllable recvfrom/sendto behavior
 * ========================================================================== */

static uint8_t  s_fake_rx_data[2048];
static size_t  s_fake_rx_len  = 0;
static size_t  s_fake_rx_pos  = 0;
static size_t  s_fake_tx_max_write = 0;
static int     s_fake_tx_fail       = 0;
static uint8_t s_fake_tx_written[4096];
static size_t  s_fake_tx_written_len = 0;
static uint32_t s_last_dst_ip   = 0;
static uint16_t s_last_dst_port  = 0;

static hal_err_t fake_bind(uint16_t port) { (void)port; return HAL_OK; }
static hal_err_t fake_close(void) { return HAL_OK; }
static bool fake_is_bound(void) { return true; }

static int fake_recvfrom(uint8_t* buf, size_t max_len, uint32_t* ip, uint16_t* port)
{
    (void)ip; (void)port;
    size_t avail = s_fake_rx_len - s_fake_rx_pos;
    if (avail == 0) return 0;
    size_t n = avail > max_len ? max_len : avail;
    memcpy(buf, s_fake_rx_data + s_fake_rx_pos, n);
    s_fake_rx_pos += n;
    return (int)n;
}

static int fake_sendto(const uint8_t* buf, size_t len, uint32_t ip, uint16_t port)
{
    s_last_dst_ip = ip;
    s_last_dst_port = port;
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

static const transport_udp_hal_ops_t s_fake_hal = {
    .bind = fake_bind, .close = fake_close,
    .recvfrom = fake_recvfrom, .sendto = fake_sendto,
    .is_bound = fake_is_bound,
};

static void reset_fake(void) {
    memset(s_fake_rx_data, 0, sizeof(s_fake_rx_data));
    s_fake_rx_len = 0; s_fake_rx_pos = 0;
    s_fake_tx_max_write = 0; s_fake_tx_fail = 0;
    memset(s_fake_tx_written, 0, sizeof(s_fake_tx_written));
    s_fake_tx_written_len = 0;
    s_last_dst_ip = 0; s_last_dst_port = 0;
}

static void feed_rx(const uint8_t* data, size_t len) {
    memcpy(s_fake_rx_data + s_fake_rx_len, data, len);
    s_fake_rx_len += len;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

void test_udp_init_binds_and_succeeds(void)
{
    reset_fake();
    transport_udp_t udp;
    transport_udp_config_t cfg = { .local_port = 9999, .remote_ip = 0xFFFFFFFF, .remote_port = 9999 };
    TEST_ASSERT_EQUAL(HAL_OK, transport_udp_init(&udp, &cfg));
    TEST_ASSERT_TRUE(transport_udp_is_bound(&udp));
    TEST_ASSERT_EQUAL(9999, udp.local_port);
    TEST_ASSERT_NOT_NULL(udp.component.service_step);
}

void test_udp_set_hal_ops(void)
{
    reset_fake();
    transport_udp_t udp;
    transport_udp_config_t cfg = { .local_port = 8888, .remote_ip = 0, .remote_port = 0 };
    transport_udp_init(&udp, &cfg);
    transport_udp_set_hal_ops(&udp, &s_fake_hal);
    TEST_ASSERT_TRUE(transport_udp_is_bound(&udp));
}

void test_udp_rx_queue_filled(void)
{
    reset_fake();
    transport_udp_t udp;
    transport_udp_config_t cfg = { .local_port = 9999, .remote_ip = 0xFFFFFFFF, .remote_port = 9999 };
    transport_udp_init(&udp, &cfg);
    transport_udp_set_hal_ops(&udp, &s_fake_hal);

    uint8_t data[] = "UDP_DATA_RX";
    feed_rx(data, 11);

    transport_udp_service_step(&udp.component, 1000);

    uint8_t out[16] = {0};
    size_t n = transport_udp_rx_read(&udp, out, sizeof(out));
    TEST_ASSERT_EQUAL(11, n);
    TEST_ASSERT_EQUAL_MEMORY("UDP_DATA_RX", out, 11);
}

void test_udp_tx_queue_drained(void)
{
    reset_fake();
    transport_udp_t udp;
    transport_udp_config_t cfg = { .local_port = 9999, .remote_ip = 0xC0A80001, .remote_port = 9999 };
    transport_udp_init(&udp, &cfg);
    transport_udp_set_hal_ops(&udp, &s_fake_hal);

    uint8_t data[] = "TX_PAYLOAD";
    transport_udp_tx_write(&udp, data, 10);

    transport_udp_service_step(&udp.component, 1000);

    TEST_ASSERT_EQUAL(10, s_fake_tx_written_len);
    TEST_ASSERT_EQUAL(0xC0A80001, s_last_dst_ip);
    TEST_ASSERT_EQUAL(9999, s_last_dst_port);
}

void test_udp_partial_write_no_data_loss(void)
{
    reset_fake();
    s_fake_tx_max_write = 4;

    transport_udp_t udp;
    transport_udp_config_t cfg = { .local_port = 9999, .remote_ip = 0, .remote_port = 9999 };
    transport_udp_init(&udp, &cfg);
    transport_udp_set_hal_ops(&udp, &s_fake_hal);

    uint8_t data[10];
    memset(data, 0x42, 10);
    transport_udp_tx_write(&udp, data, 10);

    for (int i = 0; i < 20; i++) {
        transport_udp_service_step(&udp.component, 1000 * (uint64_t)(i + 1));
    }

    TEST_ASSERT_EQUAL(10, s_fake_tx_written_len);
    /* All bytes should be 0x42 */
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL(0x42, s_fake_tx_written[i]);
    }
}

void test_udp_diagnostics(void)
{
    reset_fake();
    transport_udp_t udp;
    transport_udp_config_t cfg = { .local_port = 9999, .remote_ip = 0, .remote_port = 0 };
    transport_udp_init(&udp, &cfg);

    transport_udp_diagnostics_t diag;
    TEST_ASSERT_EQUAL(HAL_OK, transport_udp_get_diagnostics(&udp, &diag));
    TEST_ASSERT_EQUAL(0, diag.rx_total);
    TEST_ASSERT_EQUAL(0, diag.tx_total);
    TEST_ASSERT_EQUAL(0, diag.packets_rx);
    TEST_ASSERT_EQUAL(0, diag.packets_tx);
    TEST_ASSERT_EQUAL(TRANSPORT_UDP_TX_BUFFER_SIZE, diag.tx_free);
}

void test_udp_reset(void)
{
    reset_fake();
    transport_udp_t udp;
    transport_udp_config_t cfg = { .local_port = 7777, .remote_ip = 0xABCD, .remote_port = 8888 };
    transport_udp_init(&udp, &cfg);

    uint8_t data[] = "X";
    transport_udp_tx_write(&udp, data, 1);

    TEST_ASSERT_EQUAL(HAL_OK, transport_udp_reset(&udp));
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&udp.rx_buffer));
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&udp.tx_buffer));
    TEST_ASSERT_EQUAL(7777, udp.local_port);
    TEST_ASSERT_EQUAL(0xABCD, udp.remote_ip);
    TEST_ASSERT_EQUAL(8888, udp.remote_port);
    /* Bound state preserved */
    TEST_ASSERT_TRUE(transport_udp_is_bound(&udp));
}

void test_udp_null_safety(void)
{
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, transport_udp_init(NULL, &(transport_udp_config_t){0}));
    TEST_ASSERT_EQUAL(0, transport_udp_rx_read(NULL, (uint8_t[]){0}, 1));
    TEST_ASSERT_EQUAL(0, transport_udp_tx_write(NULL, (uint8_t[]){0}, 1));
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, transport_udp_reset(NULL));
}

void test_udp_contains_no_aog_logic(void)
{
    /* Compile-time check: transport_udp.h must not include AOG/PGN headers */
    reset_fake();
    transport_udp_t udp;
    transport_udp_config_t cfg = { .local_port = 9999, .remote_ip = 0, .remote_port = 0 };
    transport_udp_init(&udp, &cfg);
    /* Just raw UDP — no PGN parsing, no AOG protocol */
    TEST_ASSERT_EQUAL(0, transport_udp_rx_available(&udp));
}

/* ---- Unity ---- */

void setUp(void) { reset_fake(); }
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_udp_init_binds_and_succeeds);
    RUN_TEST(test_udp_set_hal_ops);
    RUN_TEST(test_udp_rx_queue_filled);
    RUN_TEST(test_udp_tx_queue_drained);
    RUN_TEST(test_udp_partial_write_no_data_loss);
    RUN_TEST(test_udp_diagnostics);
    RUN_TEST(test_udp_reset);
    RUN_TEST(test_udp_null_safety);
    RUN_TEST(test_udp_contains_no_aog_logic);
    return UNITY_END();
}
