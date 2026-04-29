#include "unity.h"
#include "hal_uart.h"
#include <string.h>

/* ---- Helpers ---- */

/* Our own stub ops for testing — records calls and supports partial writes */
static int s_stub_write_return = 0;       /* bytes "written" by stub write */
static int s_stub_read_return = 0;        /* bytes "read" by stub read */
static int s_stub_read_fill = 0x42;       /* byte value to fill read buffer */
static int s_stub_init_called = 0;
static int s_stub_deinit_called = 0;
static int s_stub_flush_called = 0;
static int s_stub_reset_called = 0;
static int s_stub_available_return = 0;

static hal_err_t test_stub_init(board_uart_port_t port, const hal_uart_config_t* config)
{
    (void)port;
    (void)config;
    s_stub_init_called++;
    return HAL_OK;
}

static hal_err_t test_stub_deinit(board_uart_port_t port)
{
    (void)port;
    s_stub_deinit_called++;
    return HAL_OK;
}

static int test_stub_read(board_uart_port_t port, uint8_t* buf, size_t max_len)
{
    (void)port;
    size_t n = (s_stub_read_return > 0 && (size_t)s_stub_read_return <= max_len)
               ? (size_t)s_stub_read_return : 0;
    for (size_t i = 0; i < n; i++) {
        buf[i] = (uint8_t)s_stub_read_fill;
    }
    return (int)n;
}

static int test_stub_write(board_uart_port_t port, const uint8_t* buf, size_t len)
{
    (void)port;
    (void)buf;
    /* Simulate partial write */
    if (s_stub_write_return >= 0 && (size_t)s_stub_write_return <= len) {
        return s_stub_write_return;
    }
    return (int)len;
}

static hal_err_t test_stub_flush(board_uart_port_t port)
{
    (void)port;
    s_stub_flush_called++;
    return HAL_OK;
}

static hal_err_t test_stub_reset(board_uart_port_t port)
{
    (void)port;
    s_stub_reset_called++;
    return HAL_OK;
}

static int test_stub_available(board_uart_port_t port)
{
    (void)port;
    return s_stub_available_return;
}

static const hal_uart_ops_t s_test_ops = {
    .init      = test_stub_init,
    .deinit    = test_stub_deinit,
    .read      = test_stub_read,
    .write     = test_stub_write,
    .flush     = test_stub_flush,
    .reset     = test_stub_reset,
    .available = test_stub_available,
};

void reset_test_stubs(void)
{
    s_stub_write_return = 0;
    s_stub_read_return = 0;
    s_stub_read_fill = 0x42;
    s_stub_init_called = 0;
    s_stub_deinit_called = 0;
    s_stub_flush_called = 0;
    s_stub_reset_called = 0;
    s_stub_available_return = 0;
}

void setUp(void)
{
    reset_test_stubs();
    hal_uart_init(&s_test_ops);
}

void tearDown(void)
{
    hal_uart_deinit();
}

/* ---- Init / Deinit ---- */

void test_init_with_valid_ops_returns_ok(void)
{
    hal_uart_deinit();
    hal_err_t err = hal_uart_init(&s_test_ops);
    TEST_ASSERT_EQUAL(HAL_OK, err);
}

void test_init_with_null_ops_returns_invalid_param(void)
{
    hal_err_t err = hal_uart_init(NULL);
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, err);
}

void test_deinit_clears_ops(void)
{
    hal_uart_deinit();
    /* After deinit, port_init should return NOT_INITIALIZED */
    hal_uart_config_t cfg = HAL_UART_CONFIG_DEFAULT();
    hal_err_t err = hal_uart_port_init(BOARD_UART_CONSOLE, &cfg);
    TEST_ASSERT_EQUAL(HAL_ERR_NOT_INITIALIZED, err);
}

/* ---- Port init ---- */

void test_port_init_valid_port_calls_ops(void)
{
    hal_uart_config_t cfg = HAL_UART_CONFIG_DEFAULT();
    cfg.baudrate = 9600;
    hal_err_t err = hal_uart_port_init(BOARD_UART_CONSOLE, &cfg);
    TEST_ASSERT_EQUAL(HAL_OK, err);
    TEST_ASSERT_EQUAL(1, s_stub_init_called);
}

void test_port_init_null_config_returns_invalid_param(void)
{
    hal_err_t err = hal_uart_port_init(BOARD_UART_CONSOLE, NULL);
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, err);
}

/* ---- Port deinit ---- */

void test_port_deinit_calls_ops(void)
{
    hal_err_t err = hal_uart_port_deinit(BOARD_UART_CONSOLE);
    TEST_ASSERT_EQUAL(HAL_OK, err);
    TEST_ASSERT_EQUAL(1, s_stub_deinit_called);
}

/* ---- Read ---- */

void test_read_returns_stub_value(void)
{
    s_stub_read_return = 5;
    uint8_t buf[10] = {0};
    int n = hal_uart_read(BOARD_UART_CONSOLE, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(5, n);
    TEST_ASSERT_EQUAL(0x42, buf[0]);  /* filled with test value */
}

void test_read_returns_zero_when_stub_returns_zero(void)
{
    s_stub_read_return = 0;
    uint8_t buf[10] = {0};
    int n = hal_uart_read(BOARD_UART_CONSOLE, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(0, n);
}

/* ---- Write ---- */

void test_write_returns_stub_value(void)
{
    s_stub_write_return = 3;
    uint8_t data[] = {1, 2, 3, 4, 5};
    int n = hal_uart_write(BOARD_UART_CONSOLE, data, sizeof(data));
    TEST_ASSERT_EQUAL(3, n);
}

void test_write_returns_full_length_when_stub_allows(void)
{
    s_stub_write_return = 99;  /* larger than data length */
    uint8_t data[] = {1, 2, 3, 4, 5};
    int n = hal_uart_write(BOARD_UART_CONSOLE, data, sizeof(data));
    TEST_ASSERT_EQUAL(5, n);
}

/* ---- Flush ---- */

void test_flush_calls_ops(void)
{
    hal_err_t err = hal_uart_flush(BOARD_UART_CONSOLE);
    TEST_ASSERT_EQUAL(HAL_OK, err);
    TEST_ASSERT_EQUAL(1, s_stub_flush_called);
}

/* ---- Reset ---- */

void test_reset_calls_ops(void)
{
    hal_err_t err = hal_uart_reset(BOARD_UART_CONSOLE);
    TEST_ASSERT_EQUAL(HAL_OK, err);
    TEST_ASSERT_EQUAL(1, s_stub_reset_called);
}

/* ---- Available ---- */

void test_available_returns_stub_value(void)
{
    s_stub_available_return = 42;
    int avail = hal_uart_available(BOARD_UART_CONSOLE);
    TEST_ASSERT_EQUAL(42, avail);
}

void test_available_returns_zero_by_default(void)
{
    int avail = hal_uart_available(BOARD_UART_CONSOLE);
    TEST_ASSERT_EQUAL(0, avail);
}

/* ---- Operations without init ---- */

void test_port_init_without_init_returns_not_initialized(void)
{
    hal_uart_deinit();
    hal_uart_config_t cfg = HAL_UART_CONFIG_DEFAULT();
    hal_err_t err = hal_uart_port_init(BOARD_UART_CONSOLE, &cfg);
    TEST_ASSERT_EQUAL(HAL_ERR_NOT_INITIALIZED, err);
}

void test_read_without_init_returns_zero(void)
{
    hal_uart_deinit();
    uint8_t buf[10];
    int n = hal_uart_read(BOARD_UART_CONSOLE, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(0, n);
}

void test_write_without_init_returns_zero(void)
{
    hal_uart_deinit();
    uint8_t data[] = {1};
    int n = hal_uart_write(BOARD_UART_CONSOLE, data, 1);
    TEST_ASSERT_EQUAL(0, n);
}

void test_flush_without_init_returns_not_initialized(void)
{
    hal_uart_deinit();
    hal_err_t err = hal_uart_flush(BOARD_UART_CONSOLE);
    TEST_ASSERT_EQUAL(HAL_ERR_NOT_INITIALIZED, err);
}

void test_reset_without_init_returns_not_initialized(void)
{
    hal_uart_deinit();
    hal_err_t err = hal_uart_reset(BOARD_UART_CONSOLE);
    TEST_ASSERT_EQUAL(HAL_ERR_NOT_INITIALIZED, err);
}

void test_available_without_init_returns_zero(void)
{
    hal_uart_deinit();
    int avail = hal_uart_available(BOARD_UART_CONSOLE);
    TEST_ASSERT_EQUAL(0, avail);
}

/* ---- Stub ops provider ---- */

void test_stub_ops_provider_returns_non_null(void)
{
    const hal_uart_ops_t* ops = hal_uart_stub_ops();
    TEST_ASSERT_NOT_NULL(ops);
    TEST_ASSERT_NOT_NULL(ops->init);
    TEST_ASSERT_NOT_NULL(ops->deinit);
    TEST_ASSERT_NOT_NULL(ops->read);
    TEST_ASSERT_NOT_NULL(ops->write);
    TEST_ASSERT_NOT_NULL(ops->flush);
    TEST_ASSERT_NOT_NULL(ops->reset);
    TEST_ASSERT_NOT_NULL(ops->available);
}

void test_stub_ops_returns_not_supported_for_init(void)
{
    const hal_uart_ops_t* ops = hal_uart_stub_ops();
    hal_uart_config_t cfg = HAL_UART_CONFIG_DEFAULT();
    hal_err_t err = ops->init(BOARD_UART_CONSOLE, &cfg);
    TEST_ASSERT_EQUAL(HAL_ERR_NOT_SUPPORTED, err);
}

void test_stub_ops_returns_zero_for_read(void)
{
    const hal_uart_ops_t* ops = hal_uart_stub_ops();
    uint8_t buf[10];
    int n = ops->read(BOARD_UART_CONSOLE, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(0, n);
}

void test_stub_ops_returns_zero_for_write(void)
{
    const hal_uart_ops_t* ops = hal_uart_stub_ops();
    uint8_t data[] = {1, 2, 3};
    int n = ops->write(BOARD_UART_CONSOLE, data, sizeof(data));
    TEST_ASSERT_EQUAL(0, n);
}

void test_stub_ops_returns_zero_for_available(void)
{
    const hal_uart_ops_t* ops = hal_uart_stub_ops();
    int n = ops->available(BOARD_UART_CONSOLE);
    TEST_ASSERT_EQUAL(0, n);
}

void test_stub_ops_returns_not_supported_for_flush(void)
{
    const hal_uart_ops_t* ops = hal_uart_stub_ops();
    hal_err_t err = ops->flush(BOARD_UART_CONSOLE);
    TEST_ASSERT_EQUAL(HAL_ERR_NOT_SUPPORTED, err);
}

void test_stub_ops_returns_not_supported_for_reset(void)
{
    const hal_uart_ops_t* ops = hal_uart_stub_ops();
    hal_err_t err = ops->reset(BOARD_UART_CONSOLE);
    TEST_ASSERT_EQUAL(HAL_ERR_NOT_SUPPORTED, err);
}

/* ---- Config default macro ---- */

void test_config_default_macro(void)
{
    hal_uart_config_t cfg = HAL_UART_CONFIG_DEFAULT();
    TEST_ASSERT_EQUAL(115200, cfg.baudrate);
    TEST_ASSERT_EQUAL(8, cfg.data_bits);
    TEST_ASSERT_EQUAL(1, cfg.stop_bits);
    TEST_ASSERT_EQUAL(0, cfg.parity);
    TEST_ASSERT_EQUAL(-1, cfg.tx_pin);
    TEST_ASSERT_EQUAL(-1, cfg.rx_pin);
    TEST_ASSERT_EQUAL(0, cfg.rx_buffer_size);
    TEST_ASSERT_EQUAL(0, cfg.tx_buffer_size);
}

/* ---- Null safety ---- */

void test_port_init_null_uart_returns_ok_after_deinit(void)
{
    /* hal_uart_port_init with deinit'd ops returns NOT_INITIALIZED, not crash */
    hal_uart_deinit();
    hal_uart_config_t cfg = HAL_UART_CONFIG_DEFAULT();
    hal_err_t err = hal_uart_port_init(BOARD_UART_CONSOLE, &cfg);
    TEST_ASSERT_EQUAL(HAL_ERR_NOT_INITIALIZED, err);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_with_valid_ops_returns_ok);
    RUN_TEST(test_init_with_null_ops_returns_invalid_param);
    RUN_TEST(test_deinit_clears_ops);
    RUN_TEST(test_port_init_valid_port_calls_ops);
    RUN_TEST(test_port_init_null_config_returns_invalid_param);
    RUN_TEST(test_port_deinit_calls_ops);
    RUN_TEST(test_read_returns_stub_value);
    RUN_TEST(test_read_returns_zero_when_stub_returns_zero);
    RUN_TEST(test_write_returns_stub_value);
    RUN_TEST(test_write_returns_full_length_when_stub_allows);
    RUN_TEST(test_flush_calls_ops);
    RUN_TEST(test_reset_calls_ops);
    RUN_TEST(test_available_returns_stub_value);
    RUN_TEST(test_available_returns_zero_by_default);
    RUN_TEST(test_port_init_without_init_returns_not_initialized);
    RUN_TEST(test_read_without_init_returns_zero);
    RUN_TEST(test_write_without_init_returns_zero);
    RUN_TEST(test_flush_without_init_returns_not_initialized);
    RUN_TEST(test_reset_without_init_returns_not_initialized);
    RUN_TEST(test_available_without_init_returns_zero);
    RUN_TEST(test_stub_ops_provider_returns_non_null);
    RUN_TEST(test_stub_ops_returns_not_supported_for_init);
    RUN_TEST(test_stub_ops_returns_zero_for_read);
    RUN_TEST(test_stub_ops_returns_zero_for_write);
    RUN_TEST(test_stub_ops_returns_zero_for_available);
    RUN_TEST(test_stub_ops_returns_not_supported_for_flush);
    RUN_TEST(test_stub_ops_returns_not_supported_for_reset);
    RUN_TEST(test_config_default_macro);
    RUN_TEST(test_port_init_null_uart_returns_ok_after_deinit);
    return UNITY_END();
}
