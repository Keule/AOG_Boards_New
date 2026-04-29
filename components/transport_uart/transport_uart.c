#include "transport_uart.h"
#include "hal_uart.h"
#include <string.h>

/* ---- Internal helpers ---- */

/* Forward cast: runtime_component_t* → transport_uart_t*
 * Safe because runtime_component_t is the first field. */
static transport_uart_t* comp_to_uart(runtime_component_t* comp)
{
    return (transport_uart_t*)comp;
}

/* ---- Init ---- */

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

/* ---- Service Step ----
 *
 * RX: read from HAL UART into RX buffer (128-byte bursts for 921600 baud).
 * TX: drain TX buffer to HAL UART.
 *     PARTIAL WRITE SAFE: only consumes bytes actually written by HAL.
 *     Unwritten bytes are pushed back into the ring buffer.
 */

void transport_uart_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    (void)timestamp_us;
    transport_uart_t* uart = comp_to_uart(comp);
    if (uart == NULL) {
        return;
    }

    /* ---- RX: HAL UART → rx_buffer ---- */
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

    /* Track RX highwater mark */
    size_t rx_used = byte_ring_buffer_available(&uart->rx_buffer);
    if (rx_used > uart->rx_highwater) {
        uart->rx_highwater = rx_used;
    }

    /* ---- TX: tx_buffer → HAL UART (partial write safe) ---- */
    uint8_t tx_tmp[128];
    while (byte_ring_buffer_available(&uart->tx_buffer) > 0) {
        size_t avail = byte_ring_buffer_available(&uart->tx_buffer);
        size_t chunk = avail > sizeof(tx_tmp) ? sizeof(tx_tmp) : avail;
        size_t drained = byte_ring_buffer_read(&uart->tx_buffer, tx_tmp, chunk);
        if (drained == 0) {
            break;
        }

        int written = hal_uart_write(uart->port, tx_tmp, drained);

        if (written <= 0) {
            /* HAL cannot accept any data — push everything back */
            byte_ring_buffer_write(&uart->tx_buffer, tx_tmp, drained);
            uart->tx_backpressure_count++;
            break;
        }

        uart->tx_total += (uint32_t)written;

        if ((size_t)written < drained) {
            /* Partial write — push unwritten bytes back to ring buffer.
             * Order is preserved: remaining bytes are written back after
             * any bytes that might have been added by producers between
             * our read and now. Since service_step runs in a single task,
             * no concurrent writes can occur, so FIFO order is maintained. */
            byte_ring_buffer_write(&uart->tx_buffer,
                                   tx_tmp + written,
                                   drained - (size_t)written);
            uart->tx_backpressure_count++;
            break;  /* HAL is backed up — retry next service_step call */
        }
    }
}

/* ---- Public API ---- */

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

/* ---- Statistics ---- */

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

hal_err_t transport_uart_get_diagnostics(const transport_uart_t* uart, transport_uart_diagnostics_t* diag)
{
    if (uart == NULL || diag == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    diag->rx_total              = uart->rx_total;
    diag->tx_total              = uart->tx_total;
    diag->rx_overflows          = uart->rx_overflows;
    diag->rx_highwater          = uart->rx_highwater;
    diag->tx_backpressure_count = uart->tx_backpressure_count;
    diag->rx_available          = byte_ring_buffer_available(&uart->rx_buffer);
    diag->tx_free               = (uart->tx_buffer.capacity >= uart->tx_buffer.size)
                                    ? (uart->tx_buffer.capacity - uart->tx_buffer.size)
                                    : 0;

    return HAL_OK;
}

/* ---- Reset / Recovery ---- */

hal_err_t transport_uart_reset(transport_uart_t* uart)
{
    if (uart == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    /* Step 1: Flush any pending data from HAL UART */
    hal_uart_flush(uart->port);

    /* Step 2: Clear and reinitialize ring buffers */
    byte_ring_buffer_init(&uart->rx_buffer, uart->rx_storage, sizeof(uart->rx_storage));
    byte_ring_buffer_init(&uart->tx_buffer, uart->tx_storage, sizeof(uart->tx_storage));

    /* Step 3: Reset all statistics counters */
    uart->rx_total              = 0;
    uart->tx_total              = 0;
    uart->rx_overflows          = 0;
    uart->rx_highwater          = 0;
    uart->tx_backpressure_count = 0;

    /* Port and baudrate config are preserved */

    return HAL_OK;
}
