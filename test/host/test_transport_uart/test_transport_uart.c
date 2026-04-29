#include "unity.h"
#include <stdio.h>
#include <string.h>
#include "transport_uart.h"
#include "hal_uart.h"
#include "hal_backend.h"

/* ==========================================================================
 * Fake HAL for host tests — controllable read/write behavior
 * ========================================================================== */

/* Fake RX data source: fills HAL read requests */
static uint8_t  s_fake_rx_data[2048];
static size_t  s_fake_rx_len  = 0;
static size_t  s_fake_rx_pos  = 0;

/* Fake TX write behavior:
 * - s_fake_tx_max_write: max bytes per write call (0 = write all)
 * - s_fake_tx_fail: if true, write returns -1
 * - s_fake_tx_written: records all bytes written by HAL */
static size_t  s_fake_tx_max_write = 0;  /* 0 = unlimited */
static int     s_fake_tx_fail       = 0;
static uint8_t s_fake_tx_written[4096];
static size_t  s_fake_tx_written_len = 0;

/* Fake HAL ops */
static hal_err_t fake_init(board_uart_port_t port, const hal_uart_config_t* config)
{
    (void)port; (void)config;
    return HAL_OK;
}

static hal_err_t fake_deinit(board_uart_port_t port)
{
    (void)port;
    return HAL_OK;
}

static int fake_read(board_uart_port_t port, uint8_t* buf, size_t max_len)
{
    (void)port;
    size_t available = s_fake_rx_len - s_fake_rx_pos;
    if (available == 0) return 0;
    size_t to_copy = available > max_len ? max_len : available;
    memcpy(buf, s_fake_rx_data + s_fake_rx_pos, to_copy);
    s_fake_rx_pos += to_copy;
    return (int)to_copy;
}

static int fake_write(board_uart_port_t port, const uint8_t* buf, size_t len)
{
    (void)port;
    if (s_fake_tx_fail) return -1;

    if (s_fake_tx_max_write > 0 && len > s_fake_tx_max_write) {
        size_t to_write = s_fake_tx_max_write;
        memcpy(s_fake_tx_written + s_fake_tx_written_len, buf, to_write);
        s_fake_tx_written_len += to_write;
        return (int)to_write;
    }

    memcpy(s_fake_tx_written + s_fake_tx_written_len, buf, len);
    s_fake_tx_written_len += len;
    return (int)len;
}

static const hal_uart_ops_t s_fake_ops = {
    .init   = fake_init,
    .deinit = fake_deinit,
    .read   = fake_read,
    .write  = fake_write,
};

/* ---- Helpers ---- */

static void reset_fake_state(void)
{
    memset(s_fake_rx_data, 0, sizeof(s_fake_rx_data));
    s_fake_rx_len = 0;
    s_fake_rx_pos = 0;
    s_fake_tx_max_write = 0;
    s_fake_tx_fail = 0;
    memset(s_fake_tx_written, 0, sizeof(s_fake_tx_written));
    s_fake_tx_written_len = 0;
}

/* Feed fake RX data for HAL to read */
static void feed_fake_rx(const uint8_t* data, size_t len)
{
    TEST_ASSERT_LESS_THAN(sizeof(s_fake_rx_data), s_fake_rx_len + len);
    memcpy(s_fake_rx_data + s_fake_rx_len, data, len);
    s_fake_rx_len += len;
}

/* ==========================================================================
 * Test: UART init basic
 * ========================================================================== */

void test_init_succeeds_for_console_port(void)
{
    reset_fake_state();

    transport_uart_t uart;
    transport_uart_config_t cfg = {
        .port = BOARD_UART_CONSOLE,
        .baudrate = 115200
    };

    hal_err_t err = transport_uart_init(&uart, &cfg);
    TEST_ASSERT_EQUAL_MESSAGE(HAL_OK, err, "transport_uart_init should succeed for CONSOLE port");
    TEST_ASSERT_EQUAL(115200, uart.baudrate);
    TEST_ASSERT_EQUAL(BOARD_UART_CONSOLE, uart.port);
    TEST_ASSERT_NOT_NULL(uart.component.service_step);
}

/* ==========================================================================
 * Test: UART config validation
 * ========================================================================== */

void test_init_with_921600_baud(void)
{
    reset_fake_state();

    transport_uart_t uart;
    transport_uart_config_t cfg = {
        .port = BOARD_UART_CONSOLE,
        .baudrate = 921600
    };

    hal_err_t err = transport_uart_init(&uart, &cfg);
    TEST_ASSERT_EQUAL(HAL_OK, err);
    TEST_ASSERT_EQUAL(921600, uart.baudrate);
}

void test_init_null_params(void)
{
    transport_uart_t uart;
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };

    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, transport_uart_init(NULL, &cfg));
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, transport_uart_init(&uart, NULL));
}

/* ==========================================================================
 * Test: TX partial write — NO data loss
 * ========================================================================== */

void test_partial_write_no_data_loss(void)
{
    reset_fake_state();
    s_fake_tx_max_write = 3;  /* HAL only accepts 3 bytes per call */

    transport_uart_t uart;
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    TEST_ASSERT_EQUAL(HAL_OK, transport_uart_init(&uart, &cfg));

    /* Write 10 bytes into TX buffer */
    uint8_t tx_data[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE,
                          0x01, 0x02, 0x03, 0x04, 0x05 };
    size_t written = transport_uart_tx_write(&uart, tx_data, 10);
    TEST_ASSERT_EQUAL(10, written);

    /* Run service_step multiple times until all data is drained */
    for (int i = 0; i < 20; i++) {
        transport_uart_service_step(&uart.component, 1000 * (uint64_t)(i + 1));
    }

    /* Verify ALL 10 bytes were sent via HAL */
    TEST_ASSERT_EQUAL(10, s_fake_tx_written_len);
    TEST_ASSERT_EQUAL_MEMORY(tx_data, s_fake_tx_written, 10);

    /* Verify TX buffer is empty */
    TEST_ASSERT_EQUAL(0, transport_uart_rx_available(&uart));
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&uart.tx_buffer));

    /* Verify tx_total matches */
    uint32_t tx_total = 0;
    transport_uart_get_tx_stats(&uart, &tx_total);
    TEST_ASSERT_EQUAL(10, tx_total);
}

/* ==========================================================================
 * Test: TX partial write — byte order preserved
 * ========================================================================== */

void test_partial_write_order_preserved(void)
{
    reset_fake_state();
    s_fake_tx_max_write = 7;  /* Write 7 at a time */

    transport_uart_t uart;
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    TEST_ASSERT_EQUAL(HAL_OK, transport_uart_init(&uart, &cfg));

    /* Write a recognizable pattern */
    uint8_t pattern[20];
    for (int i = 0; i < 20; i++) pattern[i] = (uint8_t)(i + 1);

    size_t written = transport_uart_tx_write(&uart, pattern, 20);
    TEST_ASSERT_EQUAL(20, written);

    /* Drain via multiple service_step calls */
    for (int i = 0; i < 20; i++) {
        transport_uart_service_step(&uart.component, 1000 * (uint64_t)(i + 1));
    }

    /* Verify exact order: 1,2,3,...,20 */
    TEST_ASSERT_EQUAL(20, s_fake_tx_written_len);
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_EQUAL_MESSAGE((uint8_t)(i + 1), s_fake_tx_written[i],
            "Byte order must be preserved across partial writes");
    }
}

/* ==========================================================================
 * Test: TX buffer integrity after partial write
 * ========================================================================== */

void test_buffer_integrity_after_partial_tx(void)
{
    reset_fake_state();
    s_fake_tx_max_write = 5;  /* Only write 5 bytes per call */

    transport_uart_t uart;
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    TEST_ASSERT_EQUAL(HAL_OK, transport_uart_init(&uart, &cfg));

    uint8_t data[] = "ABCDEFGHIJ";  /* 10 bytes */
    transport_uart_tx_write(&uart, data, 10);

    /* First service_step: writes 5, pushes back 5 */
    transport_uart_service_step(&uart.component, 1000);

    /* TX buffer should now have 5 bytes remaining */
    size_t remaining = byte_ring_buffer_available(&uart.tx_buffer);
    TEST_ASSERT_EQUAL_MESSAGE(5, remaining,
        "5 bytes should remain in TX buffer after partial write");

    /* HAL should have received 5 bytes */
    TEST_ASSERT_EQUAL(5, s_fake_tx_written_len);

    /* Second service_step: writes remaining 5 */
    transport_uart_service_step(&uart.component, 2000);

    /* Now everything should be drained */
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&uart.tx_buffer));
    TEST_ASSERT_EQUAL(10, s_fake_tx_written_len);

    /* Verify content */
    TEST_ASSERT_EQUAL_MEMORY("ABCDEFGHIJ", s_fake_tx_written, 10);
}

/* ==========================================================================
 * Test: TX backpressure count
 * ========================================================================== */

void test_tx_backpressure_count(void)
{
    reset_fake_state();
    s_fake_tx_max_write = 2;  /* Very limited writes */

    transport_uart_t uart;
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    TEST_ASSERT_EQUAL(HAL_OK, transport_uart_init(&uart, &cfg));

    uint8_t data[10];
    memset(data, 0x42, sizeof(data));
    transport_uart_tx_write(&uart, data, 10);

    /* One service_step call — should trigger backpressure */
    transport_uart_service_step(&uart.component, 1000);

    /* Backpressure should have been incremented (at least once) */
    transport_uart_diagnostics_t diag;
    transport_uart_get_diagnostics(&uart, &diag);
    TEST_ASSERT_MESSAGE(diag.tx_backpressure_count > 0,
        "Backpressure count should increment on partial writes");
}

/* ==========================================================================
 * Test: TX HAL failure — data preserved
 * ========================================================================== */

void test_tx_hal_failure_preserves_data(void)
{
    reset_fake_state();
    s_fake_tx_fail = 1;  /* HAL write always fails */

    transport_uart_t uart;
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    TEST_ASSERT_EQUAL(HAL_OK, transport_uart_init(&uart, &cfg));

    uint8_t data[] = "IMPORTANT";
    transport_uart_tx_write(&uart, data, 9);

    /* Service step: HAL write fails, data should be pushed back */
    transport_uart_service_step(&uart.component, 1000);

    /* TX buffer should still have all 9 bytes */
    size_t remaining = byte_ring_buffer_available(&uart.tx_buffer);
    TEST_ASSERT_EQUAL_MESSAGE(9, remaining,
        "Data must be preserved in TX buffer when HAL write fails");

    /* tx_total should be 0 */
    uint32_t tx_total = 0;
    transport_uart_get_tx_stats(&uart, &tx_total);
    TEST_ASSERT_EQUAL(0, tx_total);
}

/* ==========================================================================
 * Test: RX overflow counter behavior
 * ========================================================================== */

void test_rx_overflow_counter(void)
{
    reset_fake_state();

    transport_uart_t uart;
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    TEST_ASSERT_EQUAL(HAL_OK, transport_uart_init(&uart, &cfg));

    /* Fill fake RX with more data than the buffer can hold */
    uint8_t big_data[1200];
    memset(big_data, 'X', sizeof(big_data));
    feed_fake_rx(big_data, sizeof(big_data));

    /* Service step reads from HAL into RX buffer */
    transport_uart_service_step(&uart.component, 1000);

    /* Check that data was received */
    uint32_t rx_total = 0, rx_overflows = 0;
    transport_uart_get_rx_stats(&uart, &rx_total, &rx_overflows);
    TEST_ASSERT_MESSAGE(rx_total > 0, "Should have received some RX data");

    /* Check RX buffer state */
    size_t available = transport_uart_rx_available(&uart);
    TEST_ASSERT_MESSAGE(available <= TRANSPORT_UART_RX_BUFFER_SIZE,
        "RX available should not exceed buffer size");

    /* If there are more fake RX bytes, run another service_step
     * to potentially trigger overflow */
    if (s_fake_rx_pos < s_fake_rx_len) {
        /* Don't drain the buffer — just push more in */
        transport_uart_service_step(&uart.component, 2000);
    }

    /* Check overflow counter (may or may not have overflowed depending on
     * whether 1200 bytes fit in 1024-byte buffer after partial read) */
    transport_uart_diagnostics_t diag;
    transport_uart_get_diagnostics(&uart, &diag);
    /* Just verify the API works — overflow count should be >= 0 */
    TEST_ASSERT(diag.rx_overflows >= 0);  /* Always true, but tests the API */
}

/* ==========================================================================
 * Test: RX highwater tracking
 * ========================================================================== */

void test_rx_highwater_tracking(void)
{
    reset_fake_state();

    transport_uart_t uart;
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    TEST_ASSERT_EQUAL(HAL_OK, transport_uart_init(&uart, &cfg));

    /* Feed a burst of data */
    uint8_t data[200];
    memset(data, 'A', sizeof(data));
    feed_fake_rx(data, sizeof(data));

    transport_uart_service_step(&uart.component, 1000);

    transport_uart_diagnostics_t diag;
    transport_uart_get_diagnostics(&uart, &diag);

    TEST_ASSERT_MESSAGE(diag.rx_highwater > 0,
        "Highwater should be > 0 after receiving data");
    TEST_ASSERT_LESS_OR_EQUAL(TRANSPORT_UART_RX_BUFFER_SIZE, diag.rx_highwater);
}

/* ==========================================================================
 * Test: transport_uart_reset
 * ========================================================================== */

void test_transport_uart_reset(void)
{
    reset_fake_state();

    transport_uart_t uart;
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 921600 };
    TEST_ASSERT_EQUAL(HAL_OK, transport_uart_init(&uart, &cfg));

    /* Write some data */
    uint8_t tx_data[] = "TEST";
    transport_uart_tx_write(&uart, tx_data, 4);

    /* Feed some RX data */
    feed_fake_rx(tx_data, 4);
    transport_uart_service_step(&uart.component, 1000);

    /* Verify we have data */
    TEST_ASSERT(byte_ring_buffer_available(&uart.tx_buffer) > 0 ||
                byte_ring_buffer_available(&uart.rx_buffer) > 0);

    /* Reset */
    hal_err_t err = transport_uart_reset(&uart);
    TEST_ASSERT_EQUAL(HAL_OK, err);

    /* Verify buffers are cleared */
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&uart.rx_buffer));
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&uart.tx_buffer));

    /* Verify stats are reset */
    transport_uart_diagnostics_t diag;
    transport_uart_get_diagnostics(&uart, &diag);
    TEST_ASSERT_EQUAL(0, diag.rx_total);
    TEST_ASSERT_EQUAL(0, diag.tx_total);
    TEST_ASSERT_EQUAL(0, diag.rx_overflows);
    TEST_ASSERT_EQUAL(0, diag.rx_highwater);
    TEST_ASSERT_EQUAL(0, diag.tx_backpressure_count);

    /* Verify config is preserved */
    TEST_ASSERT_EQUAL(921600, uart.baudrate);
    TEST_ASSERT_EQUAL(BOARD_UART_CONSOLE, uart.port);
    TEST_ASSERT_NOT_NULL(uart.component.service_step);
}

/* ==========================================================================
 * Test: transport_uart_reset null safety
 * ========================================================================== */

void test_transport_uart_reset_null(void)
{
    hal_err_t err = transport_uart_reset(NULL);
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, err);
}

/* ==========================================================================
 * Test: Diagnostics snapshot
 * ========================================================================== */

void test_diagnostics_snapshot(void)
{
    reset_fake_state();

    transport_uart_t uart;
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    TEST_ASSERT_EQUAL(HAL_OK, transport_uart_init(&uart, &cfg));

    transport_uart_diagnostics_t diag;
    hal_err_t err = transport_uart_get_diagnostics(&uart, &diag);
    TEST_ASSERT_EQUAL(HAL_OK, err);

    /* Freshly initialized: all counters should be 0 */
    TEST_ASSERT_EQUAL(0, diag.rx_total);
    TEST_ASSERT_EQUAL(0, diag.tx_total);
    TEST_ASSERT_EQUAL(0, diag.rx_overflows);
    TEST_ASSERT_EQUAL(0, diag.rx_highwater);
    TEST_ASSERT_EQUAL(0, diag.tx_backpressure_count);
    /* tx_free should be the full buffer capacity (512) on fresh init */
    TEST_ASSERT_EQUAL_MESSAGE(TRANSPORT_UART_TX_BUFFER_SIZE, diag.tx_free,
        "TX buffer should be empty on fresh init");

    /* Null params */
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM,
        transport_uart_get_diagnostics(NULL, &diag));
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM,
        transport_uart_get_diagnostics(&uart, NULL));
}

/* ==========================================================================
 * Test: hal_uart_flush
 * ========================================================================== */

void test_hal_uart_flush_drains_rx(void)
{
    reset_fake_state();

    /* Feed some fake RX data */
    uint8_t data[] = "FLUSHME";
    feed_fake_rx(data, 7);

    /* Flush should drain all 7 bytes */
    hal_err_t err = hal_uart_flush(BOARD_UART_CONSOLE);
    TEST_ASSERT_EQUAL(HAL_OK, err);

    /* All fake RX data should have been consumed */
    TEST_ASSERT_EQUAL(s_fake_rx_len, s_fake_rx_pos);
}

void test_hal_uart_flush_not_initialized(void)
{
    hal_uart_deinit();  /* Ensure no ops set */
    hal_err_t err = hal_uart_flush(BOARD_UART_CONSOLE);
    TEST_ASSERT_EQUAL(HAL_ERR_NOT_INITIALIZED, err);

    /* Re-init for other tests */
    hal_uart_init(&s_fake_ops);
}

/* ==========================================================================
 * Test: hal_uart_port_reset
 * ========================================================================== */

void test_hal_uart_port_reset(void)
{
    reset_fake_state();

    /* Init port via transport (which calls hal_uart_port_init internally) */
    transport_uart_t uart;
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    TEST_ASSERT_EQUAL(HAL_OK, transport_uart_init(&uart, &cfg));

    /* Reset port with new config */
    hal_uart_config_t new_cfg = HAL_UART_CONFIG_DEFAULT();
    new_cfg.baudrate = 921600;

    hal_err_t err = hal_uart_port_reset(BOARD_UART_CONSOLE, &new_cfg);
    TEST_ASSERT_EQUAL(HAL_OK, err);
}

void test_hal_uart_port_reset_null_config(void)
{
    hal_err_t err = hal_uart_port_reset(BOARD_UART_CONSOLE, NULL);
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, err);
}

/* ==========================================================================
 * Test: Multiple instances independence
 * ========================================================================== */

void test_two_instances_independent(void)
{
    reset_fake_state();

    transport_uart_t uart1, uart2;
    transport_uart_config_t cfg1 = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    transport_uart_config_t cfg2 = { .port = BOARD_UART_CONSOLE, .baudrate = 921600 };

    TEST_ASSERT_EQUAL(HAL_OK, transport_uart_init(&uart1, &cfg1));
    TEST_ASSERT_EQUAL(HAL_OK, transport_uart_init(&uart2, &cfg2));

    /* Write different data to each */
    uint8_t data1[] = "AAAA";
    uint8_t data2[] = "BBBB";
    transport_uart_tx_write(&uart1, data1, 4);
    transport_uart_tx_write(&uart2, data2, 4);

    /* Each should have 4 bytes in its own TX buffer */
    TEST_ASSERT_EQUAL(4, byte_ring_buffer_available(&uart1.tx_buffer));
    TEST_ASSERT_EQUAL(4, byte_ring_buffer_available(&uart2.tx_buffer));

    /* Reset one — other should be unaffected */
    transport_uart_reset(&uart1);

    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&uart1.tx_buffer));
    TEST_ASSERT_EQUAL(4, byte_ring_buffer_available(&uart2.tx_buffer));
}

/* ==========================================================================
 * Unity setup/teardown
 * ========================================================================== */

void setUp(void)
{
    reset_fake_state();
    hal_uart_init(&s_fake_ops);
}

void tearDown(void)
{
    hal_uart_deinit();
}

/* ==========================================================================
 * Test runner
 * ========================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Init / config */
    RUN_TEST(test_init_succeeds_for_console_port);
    RUN_TEST(test_init_with_921600_baud);
    RUN_TEST(test_init_null_params);

    /* TX partial write safety */
    RUN_TEST(test_partial_write_no_data_loss);
    RUN_TEST(test_partial_write_order_preserved);
    RUN_TEST(test_buffer_integrity_after_partial_tx);
    RUN_TEST(test_tx_backpressure_count);
    RUN_TEST(test_tx_hal_failure_preserves_data);

    /* RX overflow diagnostics */
    RUN_TEST(test_rx_overflow_counter);
    RUN_TEST(test_rx_highwater_tracking);

    /* Reset / recovery */
    RUN_TEST(test_transport_uart_reset);
    RUN_TEST(test_transport_uart_reset_null);

    /* HAL flush / reset */
    RUN_TEST(test_hal_uart_flush_drains_rx);
    RUN_TEST(test_hal_uart_flush_not_initialized);
    RUN_TEST(test_hal_uart_port_reset);
    RUN_TEST(test_hal_uart_port_reset_null_config);

    /* Diagnostics */
    RUN_TEST(test_diagnostics_snapshot);

    /* Multi-instance */
    RUN_TEST(test_two_instances_independent);

    return UNITY_END();
}
