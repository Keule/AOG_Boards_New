#include "hal_uart.h"

/* Generic ops dispatch layer — platform-neutral.
 *
 * The actual implementations live in:
 *   hal_uart_esp32.c  (ESP-IDF UART driver)
 *   hal_uart_stub.c   (no-op stubs for native / unsupported)
 *
 * Callers obtain the ops table via hal_uart_esp32_ops() or
 * hal_uart_stub_ops() and inject it via hal_uart_init().          */

static const hal_uart_ops_t* s_uart_ops = NULL;

/* ---- Init / Deinit ---- */

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

/* ---- Guarded dispatch helpers ---- */

static bool ops_ready(void)
{
    return s_uart_ops != NULL;
}

/* ---- Port operations ---- */

hal_err_t hal_uart_port_init(board_uart_port_t port, const hal_uart_config_t* config)
{
    if (!ops_ready()) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    if (config == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    if (!board_profile_has_uart(port)) {
        return HAL_ERR_NOT_SUPPORTED;
    }
    if (s_uart_ops->init == NULL) {
        return HAL_ERR_NOT_SUPPORTED;
    }
    return s_uart_ops->init(port, config);
}

hal_err_t hal_uart_port_deinit(board_uart_port_t port)
{
    if (!ops_ready()) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    if (s_uart_ops->deinit == NULL) {
        return HAL_ERR_NOT_SUPPORTED;
    }
    return s_uart_ops->deinit(port);
}

int hal_uart_read(board_uart_port_t port, uint8_t* buf, size_t max_len)
{
    if (!ops_ready() || s_uart_ops->read == NULL) {
        return 0;
    }
    return s_uart_ops->read(port, buf, max_len);
}

int hal_uart_write(board_uart_port_t port, const uint8_t* buf, size_t len)
{
    if (!ops_ready() || s_uart_ops->write == NULL) {
        return 0;
    }
    return s_uart_ops->write(port, buf, len);
}

hal_err_t hal_uart_flush(board_uart_port_t port)
{
    if (!ops_ready()) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    if (s_uart_ops->flush == NULL) {
        return HAL_ERR_NOT_SUPPORTED;
    }
    return s_uart_ops->flush(port);
}

hal_err_t hal_uart_reset(board_uart_port_t port)
{
    if (!ops_ready()) {
        return HAL_ERR_NOT_INITIALIZED;
    }
    if (s_uart_ops->reset == NULL) {
        return HAL_ERR_NOT_SUPPORTED;
    }
    return s_uart_ops->reset(port);
}

int hal_uart_available(board_uart_port_t port)
{
    if (!ops_ready() || s_uart_ops->available == NULL) {
        return 0;
    }
    return s_uart_ops->available(port);
}
