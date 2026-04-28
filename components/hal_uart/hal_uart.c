#include "hal_uart.h"

static const hal_uart_ops_t* s_uart_ops = NULL;

hal_err_t hal_uart_init(const hal_uart_ops_t* ops)
{
    if (ops == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    s_uart_ops = ops;
    return HAL_OK;
}

hal_err_t hal_uart_deinit(void)
{
    s_uart_ops = NULL;
    return HAL_OK;
}

hal_err_t hal_uart_port_init(board_uart_port_t port, const hal_uart_config_t* config)
{
    if (s_uart_ops == NULL || s_uart_ops->init == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    if (!board_profile_has_uart(port)) {
        return HAL_ERR_NOT_SUPPORTED;
    }
    return s_uart_ops->init(port, config);
}

hal_err_t hal_uart_port_deinit(board_uart_port_t port)
{
    if (s_uart_ops == NULL || s_uart_ops->deinit == NULL) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    return s_uart_ops->deinit(port);
}

int hal_uart_read(board_uart_port_t port, uint8_t* buf, size_t max_len)
{
    if (s_uart_ops == NULL || s_uart_ops->read == NULL) {
        return 0;
    }
    return s_uart_ops->read(port, buf, max_len);
}

int hal_uart_write(board_uart_port_t port, const uint8_t* buf, size_t len)
{
    if (s_uart_ops == NULL || s_uart_ops->write == NULL) {
        return 0;
    }
    return s_uart_ops->write(port, buf, len);
}

/* ---- ESP32 Stub Implementations ---- */

static hal_err_t esp32_uart_init(board_uart_port_t port, const hal_uart_config_t* config)
{
    (void)port; (void)config;
    return HAL_OK;
}

static hal_err_t esp32_uart_deinit(board_uart_port_t port)
{
    (void)port;
    return HAL_OK;
}

static int esp32_uart_read(board_uart_port_t port, uint8_t* buf, size_t max_len)
{
    (void)port; (void)buf; (void)max_len;
    return 0;
}

static int esp32_uart_write(board_uart_port_t port, const uint8_t* buf, size_t len)
{
    (void)port; (void)buf; (void)len;
    return 0;
}

static const hal_uart_ops_t s_esp32_uart_ops = {
    .init   = esp32_uart_init,
    .deinit = esp32_uart_deinit,
    .read   = esp32_uart_read,
    .write  = esp32_uart_write,
};

const hal_uart_ops_t* hal_uart_esp32_ops(void)
{
    return &s_esp32_uart_ops;
}
