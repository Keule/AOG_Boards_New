/* Stub UART HAL implementation.
 *
 * Compiled on native / host builds where ESP-IDF is not available.
 * All operations return safe default values and never crash.
 * NO ESP-IDF headers are included in this file.              */

#include "hal_uart.h"

/* ---- Stub ops implementation ---- */

static hal_err_t stub_uart_init(board_uart_port_t port, const hal_uart_config_t* config)
{
    (void)port;
    (void)config;
    return HAL_ERR_NOT_SUPPORTED;
}

static hal_err_t stub_uart_deinit(board_uart_port_t port)
{
    (void)port;
    return HAL_ERR_NOT_SUPPORTED;
}

static int stub_uart_read(board_uart_port_t port, uint8_t* buf, size_t max_len)
{
    (void)port;
    (void)buf;
    (void)max_len;
    return 0;
}

static int stub_uart_write(board_uart_port_t port, const uint8_t* buf, size_t len)
{
    (void)port;
    (void)buf;
    (void)len;
    return 0;
}

static hal_err_t stub_uart_flush(board_uart_port_t port)
{
    (void)port;
    return HAL_ERR_NOT_SUPPORTED;
}

static hal_err_t stub_uart_reset(board_uart_port_t port)
{
    (void)port;
    return HAL_ERR_NOT_SUPPORTED;
}

static int stub_uart_available(board_uart_port_t port)
{
    (void)port;
    return 0;
}

/* ---- Ops table ---- */

static const hal_uart_ops_t s_stub_uart_ops = {
    .init      = stub_uart_init,
    .deinit    = stub_uart_deinit,
    .read      = stub_uart_read,
    .write     = stub_uart_write,
    .flush     = stub_uart_flush,
    .reset     = stub_uart_reset,
    .available = stub_uart_available,
};

const hal_uart_ops_t* hal_uart_stub_ops(void)
{
    return &s_stub_uart_ops;
}
