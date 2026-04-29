#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "runtime_component.h"
#include "byte_ring_buffer.h"
#include "board_profile.h"
#include "hal_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Transport UART Config ---- */

#define TRANSPORT_UART_RX_BUFFER_SIZE  1024
#define TRANSPORT_UART_TX_BUFFER_SIZE  512

typedef struct {
    board_uart_port_t port;
    uint32_t baudrate;
} transport_uart_config_t;

/* ---- Transport UART Instance ----
 *
 * RX data flow: HAL UART → rx_buffer → consumer reads via rx_read()
 * TX data flow: producer writes via tx_write() → tx_buffer → HAL UART
 *
 * This component contains NO protocol logic (no NMEA, AOG, RTCM parsing).
 * It only transports bytes between HAL and ring buffers.
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting. */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    board_uart_port_t port;
    uint32_t baudrate;

    /* Statistics */
    uint32_t rx_total;      /* total bytes received */
    uint32_t tx_total;      /* total bytes transmitted */
    uint32_t rx_overflows;  /* RX buffer overflow count */

    /* RX: HAL UART → ring buffer */
    uint8_t             rx_storage[TRANSPORT_UART_RX_BUFFER_SIZE];
    byte_ring_buffer_t  rx_buffer;

    /* TX: ring buffer → HAL UART */
    uint8_t             tx_storage[TRANSPORT_UART_TX_BUFFER_SIZE];
    byte_ring_buffer_t  tx_buffer;
} transport_uart_t;

/* ---- API ---- */

/* Initialize UART transport.
 * Sets up HAL UART port, inits RX/TX ring buffers.
 * Configures service_step callback for the runtime component. */
hal_err_t transport_uart_init(transport_uart_t* uart, const transport_uart_config_t* config);

/* Service step: HAL RX → rx_buffer, tx_buffer → HAL TX.
 * Called by the runtime service loop. Non-blocking. */
void transport_uart_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* Read bytes from RX buffer (consumed by upstream components).
 * Returns number of bytes read. */
size_t transport_uart_rx_read(transport_uart_t* uart, uint8_t* buf, size_t max_len);

/* Write bytes into TX buffer (produced by upstream components).
 * Returns number of bytes written. */
size_t transport_uart_tx_write(transport_uart_t* uart, const uint8_t* data, size_t len);

/* Bytes available in RX buffer. */
size_t transport_uart_rx_available(const transport_uart_t* uart);

/* Free space in TX buffer. */
size_t transport_uart_tx_free(const transport_uart_t* uart);

/* Get RX statistics (total bytes, overflows). Returns HAL_OK on success. */
hal_err_t transport_uart_get_rx_stats(const transport_uart_t* uart, uint32_t* total, uint32_t* overflows);

/* Get TX statistics (total bytes transmitted). Returns HAL_OK on success. */
hal_err_t transport_uart_get_tx_stats(const transport_uart_t* uart, uint32_t* total);

#ifdef __cplusplus
}
#endif
