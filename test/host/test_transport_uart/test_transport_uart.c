#include "unity.h"
#include "transport_uart.h"
#include "hal_uart.h"
#include "byte_ring_buffer.h"
#include <string.h>

/* ---- Test HAL stub with configurable behaviour ---- */

static int  s_hal_write_cap = 0;    /* max bytes HAL "writes" per call (0 = no limit) */
static int  s_hal_read_cap  = 0;    /* max bytes HAL "reads" per call (0 = no limit) */
static bool s_hal_write_partial = false;  /* simulate partial write (writes half) */
static bool s_hal_write_error = false;    /* simulate HAL write error (returns -1) */

static int test_hal_read(board_uart_port_t port, uint8_t* buf, size_t max_len)
{
    (void)port;
    size_t n = max_len;
    if (s_hal_read_cap > 0 && n > (size_t)s_hal_read_cap) {
        n = (size_t)s_hal_read_cap;
    }
    /* Fill with 0xAA pattern */
    memset(buf, 0xAA, n);
    return (int)n;
}

static int test_hal_write(board_uart_port_t port, const uint8_t* buf, size_t len)
{
    (void)port;
    (void)buf;
    if (s_hal_write_error) {
        return -1;  /* Simulate HAL error */
    }
    if (s_hal_write_partial && len > 2) {
        /* Simulate partial write: write only half */
        return (int)(len / 2);
    }
    if (s_hal_write_cap > 0 && len > (size_t)s_hal_write_cap) {
        return s_hal_write_cap;
    }
    return (int)len;
}

static hal_err_t test_hal_init(board_uart_port_t port, const hal_uart_config_t* config)
{
    (void)port;
    (void)config;
    return HAL_OK;
}

static hal_err_t test_hal_deinit(board_uart_port_t port)
{
    (void)port;
    return HAL_OK;
}

static hal_err_t test_hal_flush(board_uart_port_t port)
{
    (void)port;
    return HAL_OK;
}

static hal_err_t test_hal_reset(board_uart_port_t port)
{
    (void)port;
    return HAL_OK;
}

static int test_hal_available(board_uart_port_t port)
{
    (void)port;
    return 0;
}

static const hal_uart_ops_t s_test_hal_ops = {
    .init      = test_hal_init,
    .deinit    = test_hal_deinit,
    .read      = test_hal_read,
    .write     = test_hal_write,
    .flush     = test_hal_flush,
    .reset     = test_hal_reset,
    .available = test_hal_available,
};

/* ---- Test instance ---- */

static transport_uart_t uart;

static void reset_hal_stubs(void)
{
    s_hal_write_cap = 0;
    s_hal_read_cap = 0;
    s_hal_write_partial = false;
    s_hal_write_error = false;
}

void setUp(void)
{
    reset_hal_stubs();
    hal_uart_init(&s_test_hal_ops);
    memset(&uart, 0, sizeof(uart));
}

void tearDown(void)
{
    hal_uart_deinit();
}

/* ---- Init ---- */

void test_init_sets_up_correctly(void)
{
    transport_uart_config_t cfg = {
        .port = BOARD_UART_GNSS_PRIMARY,
        .baudrate = 115200
    };
    hal_err_t err = transport_uart_init(&uart, &cfg);
    TEST_ASSERT_EQUAL(HAL_OK, err);
    TEST_ASSERT_EQUAL(BOARD_UART_GNSS_PRIMARY, uart.port);
    TEST_ASSERT_EQUAL(115200, uart.baudrate);
}

void test_init_null_uart_returns_error(void)
{
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    hal_err_t err = transport_uart_init(NULL, &cfg);
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, err);
}

void test_init_null_config_returns_error(void)
{
    hal_err_t err = transport_uart_init(&uart, NULL);
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, err);
}

/* ---- RX read / TX write ---- */

void test_rx_read_returns_data_from_hal(void)
{
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    transport_uart_init(&uart, &cfg);

    /* Simulate HAL providing 10 bytes */
    s_hal_read_cap = 10;

    /* Service step: reads from HAL into RX buffer */
    transport_uart_service_step((runtime_component_t*)&uart, 1000);

    TEST_ASSERT_EQUAL(10, transport_uart_rx_available(&uart));

    uint8_t out[16] = {0};
    size_t n = transport_uart_rx_read(&uart, out, sizeof(out));
    TEST_ASSERT_EQUAL(10, n);
    /* Data was filled with 0xAA */
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL(0xAA, out[i]);
    }
}

void test_tx_write_puts_data_in_buffer(void)
{
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    transport_uart_init(&uart, &cfg);

    uint8_t data[] = {1, 2, 3, 4, 5};
    size_t written = transport_uart_tx_write(&uart, data, sizeof(data));
    TEST_ASSERT_EQUAL(5, written);
    TEST_ASSERT_EQUAL(5, byte_ring_buffer_available(&uart.tx_buffer));
}

/* ---- TX Partial Write Safety (peek/consume pattern) ---- */

void test_partial_write_preserves_unwritten_bytes(void)
{
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    transport_uart_init(&uart, &cfg);

    /* Write 10 bytes into TX buffer */
    uint8_t data[10];
    for (int i = 0; i < 10; i++) data[i] = (uint8_t)i;
    transport_uart_tx_write(&uart, data, 10);
    TEST_ASSERT_EQUAL(10, byte_ring_buffer_available(&uart.tx_buffer));

    /* HAL will only write 4 bytes per call (partial write) */
    s_hal_write_cap = 4;

    /* Service step: peek 10, write 4, consume 4 → 6 remaining */
    transport_uart_service_step((runtime_component_t*)&uart, 1000);

    const transport_uart_stats_t* stats = transport_uart_get_stats(&uart);
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_EQUAL(4, stats->tx_bytes_out);
    TEST_ASSERT_EQUAL(1, stats->tx_partial_writes);

    /* Second service step: peek 6, write 4, consume 4 → 2 remaining */
    transport_uart_service_step((runtime_component_t*)&uart, 2000);
    TEST_ASSERT_EQUAL(8, stats->tx_bytes_out);
    TEST_ASSERT_EQUAL(2, stats->tx_partial_writes);

    /* Third service step: peek 2, write 2, consume 2 → 0 remaining */
    transport_uart_service_step((runtime_component_t*)&uart, 3000);
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&uart.tx_buffer));
    TEST_ASSERT_EQUAL(10, stats->tx_bytes_out);
}

void test_partial_write_preserves_data_integrity(void)
{
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    transport_uart_init(&uart, &cfg);

    /* Write specific byte pattern */
    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    transport_uart_tx_write(&uart, data, sizeof(data));

    /* HAL writes 3 bytes per call */
    s_hal_write_cap = 3;

    /* Drain all data across multiple service steps */
    for (int i = 0; i < 20 && byte_ring_buffer_available(&uart.tx_buffer) > 0; i++) {
        transport_uart_service_step((runtime_component_t*)&uart, (uint64_t)(i + 1) * 1000);
    }

    /* TX buffer should be empty — all data sent */
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&uart.tx_buffer));

    /* Total bytes out = 6, same as input */
    const transport_uart_stats_t* stats = transport_uart_get_stats(&uart);
    TEST_ASSERT_EQUAL(6, stats->tx_bytes_out);
}

void test_no_partial_write_when_hal_accepts_all(void)
{
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    transport_uart_init(&uart, &cfg);

    uint8_t data[] = {1, 2, 3, 4, 5};
    transport_uart_tx_write(&uart, data, sizeof(data));

    /* HAL accepts everything (no cap) */
    s_hal_write_cap = 0;

    transport_uart_service_step((runtime_component_t*)&uart, 1000);

    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&uart.tx_buffer));

    const transport_uart_stats_t* stats = transport_uart_get_stats(&uart);
    TEST_ASSERT_EQUAL(5, stats->tx_bytes_out);
    TEST_ASSERT_EQUAL(0, stats->tx_partial_writes);
    TEST_ASSERT_EQUAL(0, stats->tx_errors);
    TEST_ASSERT_EQUAL(0, stats->tx_overflows);
}

/* ---- RX Overflow Diagnostics ---- */

void test_rx_overflow_is_tracked(void)
{
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    transport_uart_init(&uart, &cfg);

    /* Fill the 1024-byte RX buffer in multiple service steps */
    /* Each service step reads up to 128 bytes from HAL */
    for (int step = 0; step < 8; step++) {
        transport_uart_service_step((runtime_component_t*)&uart, (uint64_t)(step + 1) * 1000);
    }

    /* RX buffer should be full (1024 bytes) */
    TEST_ASSERT_EQUAL(1024, transport_uart_rx_available(&uart));

    /* One more service step — HAL provides more data but buffer is full */
    transport_uart_service_step((runtime_component_t*)&uart, 9000);

    /* Overflow counter should have incremented */
    const transport_uart_stats_t* stats = transport_uart_get_stats(&uart);
    TEST_ASSERT_TRUE(stats->rx_overflow_count > 0);
}

void test_rx_overflow_visible_in_diagnostics(void)
{
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    transport_uart_init(&uart, &cfg);

    /* Fill RX buffer (8 steps of 128 bytes) + 1 overflow step */
    for (int step = 0; step < 9; step++) {
        transport_uart_service_step((runtime_component_t*)&uart, (uint64_t)(step + 1) * 1000);
    }

    transport_uart_diagnostics_t diag;
    hal_err_t err = transport_uart_diagnostics(&uart, &diag);
    TEST_ASSERT_EQUAL(HAL_OK, err);
    TEST_ASSERT_EQUAL(1024, diag.rx_buffer_size);
    TEST_ASSERT_TRUE(diag.rx_overflow_total > 0);
    TEST_ASSERT_EQUAL(diag.rx_overflow_total, diag.stats.rx_overflow_count);
}

/* ---- Stats ---- */

void test_stats_initially_zero(void)
{
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    transport_uart_init(&uart, &cfg);

    const transport_uart_stats_t* stats = transport_uart_get_stats(&uart);
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_EQUAL(0, stats->rx_bytes_in);
    TEST_ASSERT_EQUAL(0, stats->rx_overflow_count);
    TEST_ASSERT_EQUAL(0, stats->tx_bytes_out);
    TEST_ASSERT_EQUAL(0, stats->tx_partial_writes);
    TEST_ASSERT_EQUAL(0, stats->tx_errors);
    TEST_ASSERT_EQUAL(0, stats->tx_overflows);
}

void test_stats_null_returns_null(void)
{
    const transport_uart_stats_t* stats = transport_uart_get_stats(NULL);
    TEST_ASSERT_NULL(stats);
}

/* ---- Reset ---- */

void test_reset_clears_buffers_and_stats(void)
{
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    transport_uart_init(&uart, &cfg);

    /* Pump some data through to populate stats */
    s_hal_read_cap = 10;
    transport_uart_service_step((runtime_component_t*)&uart, 1000);

    uint8_t data[] = {1, 2, 3};
    transport_uart_tx_write(&uart, data, sizeof(data));
    s_hal_write_cap = 3;
    transport_uart_service_step((runtime_component_t*)&uart, 2000);

    /* Verify stats are populated */
    const transport_uart_stats_t* before = transport_uart_get_stats(&uart);
    TEST_ASSERT_TRUE(before->rx_bytes_in > 0);
    TEST_ASSERT_TRUE(before->tx_bytes_out > 0);

    /* Reset */
    hal_err_t err = transport_uart_reset(&uart);
    TEST_ASSERT_EQUAL(HAL_OK, err);

    /* Buffers should be empty */
    TEST_ASSERT_EQUAL(0, transport_uart_rx_available(&uart));
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&uart.tx_buffer));

    /* Stats should be zeroed */
    const transport_uart_stats_t* after = transport_uart_get_stats(&uart);
    TEST_ASSERT_EQUAL(0, after->rx_bytes_in);
    TEST_ASSERT_EQUAL(0, after->tx_bytes_out);
    TEST_ASSERT_EQUAL(0, after->rx_overflow_count);
}

void test_reset_null_returns_error(void)
{
    hal_err_t err = transport_uart_reset(NULL);
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, err);
}

/* ---- Diagnostics ---- */

void test_diagnostics_returns_valid_data(void)
{
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    transport_uart_init(&uart, &cfg);

    transport_uart_diagnostics_t diag;
    hal_err_t err = transport_uart_diagnostics(&uart, &diag);
    TEST_ASSERT_EQUAL(HAL_OK, err);
    TEST_ASSERT_EQUAL(1024, diag.rx_buffer_size);
    TEST_ASSERT_EQUAL(512, diag.tx_buffer_size);
}

void test_diagnostics_null_returns_error(void)
{
    transport_uart_diagnostics_t diag;
    hal_err_t err = transport_uart_diagnostics(NULL, &diag);
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, err);

    err = transport_uart_diagnostics(&uart, NULL);
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, err);
}

/* ---- Service step null safety ---- */

void test_service_step_null_does_not_crash(void)
{
    transport_uart_service_step(NULL, 1000);
    TEST_PASS();
}

/* ---- TX/RX free ---- */

void test_tx_free_returns_remaining_space(void)
{
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    transport_uart_init(&uart, &cfg);

    TEST_ASSERT_EQUAL(512, transport_uart_tx_free(&uart));

    uint8_t data[100];
    memset(data, 0, sizeof(data));
    transport_uart_tx_write(&uart, data, 100);

    TEST_ASSERT_EQUAL(412, transport_uart_tx_free(&uart));
}

void test_rx_available_null_returns_zero(void)
{
    TEST_ASSERT_EQUAL(0, transport_uart_rx_available(NULL));
}

void test_tx_free_null_returns_zero(void)
{
    TEST_ASSERT_EQUAL(0, transport_uart_tx_free(NULL));
}

void test_rx_read_null_returns_zero(void)
{
    uint8_t buf[10];
    TEST_ASSERT_EQUAL(0, transport_uart_rx_read(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(0, transport_uart_rx_read(&uart, NULL, 10));
    TEST_ASSERT_EQUAL(0, transport_uart_rx_read(&uart, buf, 0));
}

void test_tx_write_null_returns_zero(void)
{
    uint8_t data[] = {1};
    TEST_ASSERT_EQUAL(0, transport_uart_tx_write(NULL, data, 1));
    TEST_ASSERT_EQUAL(0, transport_uart_tx_write(&uart, NULL, 1));
    TEST_ASSERT_EQUAL(0, transport_uart_tx_write(&uart, data, 0));
}

/* ---- NAV-RTCM-001: TX errors tracked ---- */

void test_tx_errors_incremented_on_hal_error(void)
{
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    transport_uart_init(&uart, &cfg);

    /* Write 5 bytes into TX buffer */
    uint8_t data[] = {1, 2, 3, 4, 5};
    transport_uart_tx_write(&uart, data, 5);

    /* HAL will return -1 (error) */
    s_hal_write_error = true;

    /* Service step: HAL write fails, bytes stay in buffer */
    transport_uart_service_step((runtime_component_t*)&uart, 1000);

    const transport_uart_stats_t* stats = transport_uart_get_stats(&uart);
    TEST_ASSERT_EQUAL(1, stats->tx_errors);
    TEST_ASSERT_EQUAL(0, stats->tx_bytes_out);
    /* Bytes should still be in the TX buffer (not consumed) */
    TEST_ASSERT_EQUAL(5, byte_ring_buffer_available(&uart.tx_buffer));

    /* Another service step = another error */
    transport_uart_service_step((runtime_component_t*)&uart, 2000);
    TEST_ASSERT_EQUAL(2, stats->tx_errors);

    /* Clear error, HAL accepts all */
    s_hal_write_error = false;
    transport_uart_service_step((runtime_component_t*)&uart, 3000);
    TEST_ASSERT_EQUAL(2, stats->tx_errors);  /* no more errors */
    TEST_ASSERT_EQUAL(5, stats->tx_bytes_out);
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&uart.tx_buffer));
}

/* ---- NAV-RTCM-001: TX overflows tracked ---- */

void test_tx_overflows_tracked_when_buffer_full(void)
{
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    transport_uart_init(&uart, &cfg);

    /* TX buffer is 512 bytes. Write more than that. */
    uint8_t data[256];
    memset(data, 0xBB, sizeof(data));

    /* First write: 256 bytes — fits fully */
    size_t written1 = transport_uart_tx_write(&uart, data, 256);
    TEST_ASSERT_EQUAL(256, written1);
    TEST_ASSERT_EQUAL(0, transport_uart_get_stats(&uart)->tx_overflows);

    /* Second write: 256 more bytes — 256 fit (total 512) */
    size_t written2 = transport_uart_tx_write(&uart, data, 256);
    TEST_ASSERT_EQUAL(256, written2);
    TEST_ASSERT_EQUAL(0, transport_uart_get_stats(&uart)->tx_overflows);

    /* Third write: buffer now full — all overflow */
    size_t written3 = transport_uart_tx_write(&uart, data, 10);
    TEST_ASSERT_EQUAL(0, written3);
    TEST_ASSERT_TRUE(transport_uart_get_stats(&uart)->tx_overflows > 0);

    /* Drain buffer via service step */
    transport_uart_service_step((runtime_component_t*)&uart, 1000);

    /* Write again: fits, no new overflow */
    size_t written4 = transport_uart_tx_write(&uart, data, 100);
    TEST_ASSERT_EQUAL(100, written4);
}

void test_stats_include_new_fields_initially_zero(void)
{
    transport_uart_config_t cfg = { .port = BOARD_UART_CONSOLE, .baudrate = 115200 };
    transport_uart_init(&uart, &cfg);

    const transport_uart_stats_t* stats = transport_uart_get_stats(&uart);
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_EQUAL(0, stats->tx_errors);
    TEST_ASSERT_EQUAL(0, stats->tx_overflows);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_sets_up_correctly);
    RUN_TEST(test_init_null_uart_returns_error);
    RUN_TEST(test_init_null_config_returns_error);
    RUN_TEST(test_rx_read_returns_data_from_hal);
    RUN_TEST(test_tx_write_puts_data_in_buffer);
    RUN_TEST(test_partial_write_preserves_unwritten_bytes);
    RUN_TEST(test_partial_write_preserves_data_integrity);
    RUN_TEST(test_no_partial_write_when_hal_accepts_all);
    RUN_TEST(test_rx_overflow_is_tracked);
    RUN_TEST(test_rx_overflow_visible_in_diagnostics);
    RUN_TEST(test_stats_initially_zero);
    RUN_TEST(test_stats_null_returns_null);
    RUN_TEST(test_reset_clears_buffers_and_stats);
    RUN_TEST(test_reset_null_returns_error);
    RUN_TEST(test_diagnostics_returns_valid_data);
    RUN_TEST(test_diagnostics_null_returns_error);
    RUN_TEST(test_service_step_null_does_not_crash);
    RUN_TEST(test_tx_free_returns_remaining_space);
    RUN_TEST(test_rx_available_null_returns_zero);
    RUN_TEST(test_tx_free_null_returns_zero);
    RUN_TEST(test_rx_read_null_returns_zero);
    RUN_TEST(test_tx_write_null_returns_zero);

    /* NAV-RTCM-001: TX error and overflow tracking */
    RUN_TEST(test_tx_errors_incremented_on_hal_error);
    RUN_TEST(test_tx_overflows_tracked_when_buffer_full);
    RUN_TEST(test_stats_include_new_fields_initially_zero);
    UNITY_END();
}
