#include "transport_uart.h"
#include "hal_uart.h"
#include <string.h>

/* Forward cast: runtime_component_t* → transport_uart_t*
 * Safe because runtime_component_t is the first field. */
static transport_uart_t* comp_to_uart(runtime_component_t* comp)
{
    return (transport_uart_t*)comp;
}

hal_err_t transport_uart_init(transport_uart_t* uart, const transport_uart_config_t* config)
{
    if (uart == NULL || config == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    if (!board_profile_has_uart(config->port)) {
        return HAL_ERR_NOT_SUPPORTED;
    }

    memset(uart, 0, sizeof(transport_uart_t));

    uart->port = config->port;
    uart->baudrate = config->baudrate;

    /* Init RX/TX ring buffers */
    byte_ring_buffer_init(&uart->rx_buffer, uart->rx_storage, sizeof(uart->rx_storage));
    byte_ring_buffer_init(&uart->tx_buffer, uart->tx_storage, sizeof(uart->tx_storage));

    /* Configure HAL UART */
    hal_uart_config_t hal_cfg = HAL_UART_CONFIG_DEFAULT();
    hal_cfg.baudrate = config->baudrate;

    hal_err_t err = hal_uart_port_init(config->port, &hal_cfg);
    if (err != HAL_OK) {
        return err;
    }

    /* Register service step callback */
    uart->component.service_step = transport_uart_service_step;

    return HAL_OK;
}

void transport_uart_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    (void)timestamp_us;
    transport_uart_t* uart = comp_to_uart(comp);
    if (uart == NULL) {
        return;
    }

    /* RX: read from HAL UART into RX buffer (128-byte bursts for 921600 baud) */
    uint8_t rx_tmp[128];
    int n = hal_uart_read(uart->port, rx_tmp, sizeof(rx_tmp));
    if (n > 0) {
        uart->rx_total += (uint32_t)n;
        byte_ring_buffer_write(&uart->rx_buffer, rx_tmp, (size_t)n);
    }

    /* Track RX overflows from ring buffer */
    uint32_t new_overflows = byte_ring_buffer_overflow_count(&uart->rx_buffer);
    if (new_overflows > uart->rx_overflows) {
        uart->rx_overflows = new_overflows;
    }

    /* TX: drain TX buffer to HAL UART (128-byte bursts) */
    uint8_t tx_tmp[128];
    size_t avail = byte_ring_buffer_available(&uart->tx_buffer);
    if (avail > 0) {
        size_t to_drain = avail > sizeof(tx_tmp) ? sizeof(tx_tmp) : avail;
        size_t drained = byte_ring_buffer_read(&uart->tx_buffer, tx_tmp, to_drain);
        if (drained > 0) {
            int written = hal_uart_write(uart->port, tx_tmp, drained);
            if (written > 0) {
                uart->tx_total += (uint32_t)written;
            }
        }
    }
}

size_t transport_uart_rx_read(transport_uart_t* uart, uint8_t* buf, size_t max_len)
{
    if (uart == NULL || buf == NULL || max_len == 0) {
        return 0;
    }
    return byte_ring_buffer_read(&uart->rx_buffer, buf, max_len);
}

size_t transport_uart_tx_write(transport_uart_t* uart, const uint8_t* data, size_t len)
{
    if (uart == NULL || data == NULL || len == 0) {
        return 0;
    }
    return byte_ring_buffer_write(&uart->tx_buffer, data, len);
}

size_t transport_uart_rx_available(const transport_uart_t* uart)
{
    if (uart == NULL) {
        return 0;
    }
    return byte_ring_buffer_available(&uart->rx_buffer);
}

size_t transport_uart_tx_free(const transport_uart_t* uart)
{
    if (uart == NULL) {
        return 0;
    }
    if (uart->tx_buffer.capacity <= uart->tx_buffer.size) {
        return 0;
    }
    return uart->tx_buffer.capacity - uart->tx_buffer.size;
}

hal_err_t transport_uart_get_rx_stats(const transport_uart_t* uart, uint32_t* total, uint32_t* overflows)
{
    if (uart == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    if (total != NULL) {
        *total = uart->rx_total;
    }
    if (overflows != NULL) {
        *overflows = uart->rx_overflows;
    }
    return HAL_OK;
}

hal_err_t transport_uart_get_tx_stats(const transport_uart_t* uart, uint32_t* total)
{
    if (uart == NULL || total == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    *total = uart->tx_total;
    return HAL_OK;
}
