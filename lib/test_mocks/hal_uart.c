/*
 * Stub hal_uart.c for native tests.
 * Provides no-op/return-default implementations for all hal_uart functions.
 */
#include "hal_uart.h"
#include <string.h>

static hal_uart_ops_t s_stub_ops = {0};
static const hal_uart_ops_t* s_ops = NULL;

hal_err_t hal_uart_init(const hal_uart_ops_t* ops) {
    if (!ops) return HAL_ERR_INVALID_PARAM;
    memcpy(&s_stub_ops, ops, sizeof(hal_uart_ops_t));
    s_ops = &s_stub_ops;
    return HAL_OK;
}

hal_err_t hal_uart_deinit(void) {
    s_ops = NULL;
    memset(&s_stub_ops, 0, sizeof(hal_uart_ops_t));
    return HAL_OK;
}

hal_err_t hal_uart_port_init(board_uart_port_t port, const hal_uart_config_t* config) {
    (void)port;
    if (config == NULL) return HAL_ERR_INVALID_PARAM;
    return s_ops ? s_ops->init(port, config) : HAL_ERR_NOT_INITIALIZED;
}

hal_err_t hal_uart_port_deinit(board_uart_port_t port) {
    (void)port;
    return s_ops ? s_ops->deinit(port) : HAL_ERR_NOT_INITIALIZED;
}

int hal_uart_read(board_uart_port_t port, uint8_t* buf, size_t max_len) {
    (void)port; (void)buf; (void)max_len;
    return s_ops ? s_ops->read(port, buf, max_len) : 0;
}

int hal_uart_write(board_uart_port_t port, const uint8_t* buf, size_t len) {
    (void)port; (void)buf; (void)len;
    return s_ops ? s_ops->write(port, buf, len) : 0;
}

hal_err_t hal_uart_flush(board_uart_port_t port) {
    (void)port;
    return s_ops ? s_ops->flush(port) : HAL_ERR_NOT_INITIALIZED;
}

hal_err_t hal_uart_reset(board_uart_port_t port) {
    (void)port;
    return s_ops ? s_ops->reset(port) : HAL_ERR_NOT_INITIALIZED;
}

int hal_uart_available(board_uart_port_t port) {
    (void)port;
    return s_ops ? s_ops->available(port) : 0;
}

const hal_uart_ops_t* hal_uart_esp32_ops(void) { return NULL; }

/* ---- Default stub ops (no-op implementations) ---- */

static hal_err_t default_stub_init(board_uart_port_t port, const hal_uart_config_t* config) {
    (void)port; (void)config; return HAL_ERR_NOT_SUPPORTED; }
static hal_err_t default_stub_deinit(board_uart_port_t port) {
    (void)port; return HAL_ERR_NOT_SUPPORTED; }
static int default_stub_read(board_uart_port_t port, uint8_t* buf, size_t max_len) {
    (void)port; (void)buf; (void)max_len; return 0; }
static int default_stub_write(board_uart_port_t port, const uint8_t* buf, size_t len) {
    (void)port; (void)buf; (void)len; return 0; }
static hal_err_t default_stub_flush(board_uart_port_t port) {
    (void)port; return HAL_ERR_NOT_SUPPORTED; }
static hal_err_t default_stub_reset(board_uart_port_t port) {
    (void)port; return HAL_ERR_NOT_SUPPORTED; }
static int default_stub_available(board_uart_port_t port) {
    (void)port; return 0; }

static const hal_uart_ops_t s_default_stub_ops = {
    .init      = default_stub_init,
    .deinit    = default_stub_deinit,
    .read      = default_stub_read,
    .write     = default_stub_write,
    .flush     = default_stub_flush,
    .reset     = default_stub_reset,
    .available = default_stub_available,
};

const hal_uart_ops_t* hal_uart_stub_ops(void) { return &s_default_stub_ops; }
