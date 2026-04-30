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
    byte_ring_buffer_init(&uart->rx_buffer, uart->rx_storage, TRANSPORT_UART_RX_BUFFER_SIZE);
    byte_ring_buffer_init(&uart->tx_buffer, uart->tx_storage, TRANSPORT_UART_TX_BUFFER_SIZE);

    /* Configure HAL UART — get pins from board profile */
    hal_uart_config_t hal_cfg = HAL_UART_CONFIG_DEFAULT();
    hal_cfg.baudrate = config->baudrate;

    board_uart_pins_t uart_pins;
    if (board_profile_get_uart_pins(config->port, &uart_pins)) {
        hal_cfg.tx_pin = uart_pins.tx_pin;
        hal_cfg.rx_pin = uart_pins.rx_pin;
    }

    hal_err_t err = hal_uart_port_init(config->port, &hal_cfg);
    if (err != HAL_OK) {
        return err;
    }

    /* Register service step callback */
    uart->component.service_step = transport_uart_service_step;

    return HAL_OK;
}

hal_err_t transport_uart_reset(transport_uart_t* uart)
{
    if (uart == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    /* Flush HAL UART */
    hal_uart_flush(uart->port);

    /* Reset ring buffers (preserving storage) */
    byte_ring_buffer_init(&uart->rx_buffer, uart->rx_storage, TRANSPORT_UART_RX_BUFFER_SIZE);
    byte_ring_buffer_init(&uart->tx_buffer, uart->tx_storage, TRANSPORT_UART_TX_BUFFER_SIZE);

    /* Zero statistics */
    memset(&uart->stats, 0, sizeof(uart->stats));

    return HAL_OK;
}

void transport_uart_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    (void)timestamp_us;
    transport_uart_t* uart = comp_to_uart(comp);
    if (uart == NULL) {
        return;
    }

    /* ---- RX: read from HAL UART into RX buffer ---- */
    uint8_t rx_tmp[TRANSPORT_UART_BURST_SIZE];
    int n = hal_uart_read(uart->port, rx_tmp, sizeof(rx_tmp));
    if (n > 0) {
        size_t written = byte_ring_buffer_write(&uart->rx_buffer, rx_tmp, (size_t)n);
        uart->stats.rx_bytes_in += written;

        /* Track RX overflow from ring buffer */
        uint32_t rx_overflow = byte_ring_buffer_overflow_count(&uart->rx_buffer);
        if (rx_overflow > uart->stats.rx_overflow_count) {
            uart->stats.rx_overflow_count = rx_overflow;
        }
    }

    /* ---- TX: peek from ring buffer, write to HAL, consume written bytes ----
     * FIFO-safe: peek reads without removing, consume only removes
     * what HAL actually accepted.  No pushback needed. */
    size_t tx_avail = byte_ring_buffer_available(&uart->tx_buffer);
    if (tx_avail > 0) {
        size_t to_peek = tx_avail > TRANSPORT_UART_BURST_SIZE ? TRANSPORT_UART_BURST_SIZE : tx_avail;
        uint8_t tx_tmp[TRANSPORT_UART_BURST_SIZE];
        size_t peeked = byte_ring_buffer_peek(&uart->tx_buffer, tx_tmp, to_peek);

        if (peeked > 0) {
            int written = hal_uart_write(uart->port, tx_tmp, peeked);

            if (written >= 0 && (size_t)written < peeked) {
                /* Partial write: consume only what was actually written */
                uart->stats.tx_partial_writes++;
            }

            /* Consume all bytes that HAL accepted */
            size_t consumed = (written > 0) ? (size_t)written : 0;
            byte_ring_buffer_consume(&uart->tx_buffer, consumed);
            uart->stats.tx_bytes_out += consumed;
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

const transport_uart_stats_t* transport_uart_get_stats(const transport_uart_t* uart)
{
    if (uart == NULL) {
        return NULL;
    }
    return &uart->stats;
}

hal_err_t transport_uart_diagnostics(const transport_uart_t* uart, transport_uart_diagnostics_t* diag)
{
    if (uart == NULL || diag == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    diag->rx_buffer_size = (uint32_t)uart->rx_buffer.capacity;
    diag->rx_buffer_used = (uint32_t)uart->rx_buffer.size;
    diag->rx_buffer_free = (uint32_t)(uart->rx_buffer.capacity - uart->rx_buffer.size);
    diag->rx_overflow_total = byte_ring_buffer_overflow_count(&uart->rx_buffer);

    diag->tx_buffer_size = (uint32_t)uart->tx_buffer.capacity;
    diag->tx_buffer_used = (uint32_t)uart->tx_buffer.size;
    diag->tx_buffer_free = (uint32_t)(uart->tx_buffer.capacity - uart->tx_buffer.size);

    diag->stats = uart->stats;

    return HAL_OK;
}
