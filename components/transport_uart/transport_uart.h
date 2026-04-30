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
#define TRANSPORT_UART_BURST_SIZE      128

typedef struct {
    board_uart_port_t port;
    uint32_t baudrate;
} transport_uart_config_t;

/* ---- Transport UART Statistics ---- */

typedef struct {
    uint32_t rx_bytes_in;        /* Total bytes read from HAL into RX buffer     */
    uint32_t rx_overflow_count;  /* RX ring buffer overflow counter               */
    uint32_t tx_bytes_out;       /* Total bytes written from TX buffer to HAL     */
    uint32_t tx_partial_writes;  /* Number of times HAL wrote fewer bytes than
                                    requested (backpressure event)               */
    uint32_t tx_pushback_bytes;  /* DEPRECATED: no longer incremented.
                                    Retained for backward compatibility. */
} transport_uart_stats_t;

/* ---- Transport UART Diagnostics ---- */

typedef struct {
    uint32_t rx_buffer_size;
    uint32_t rx_buffer_used;
    uint32_t rx_buffer_free;
    uint32_t tx_buffer_size;
    uint32_t tx_buffer_used;
    uint32_t tx_buffer_free;
    uint32_t rx_overflow_total;
    transport_uart_stats_t stats;
} transport_uart_diagnostics_t;

/* ---- Transport UART Instance ----
 *
 * RX data flow: HAL UART → rx_buffer → consumer reads via rx_read()
 * TX data flow: producer writes via tx_write() → tx_buffer → HAL UART
 *
 * This component contains NO protocol logic (no NMEA, AOG, RTCM parsing).
 * It only transports bytes between HAL and ring buffers.
 *
 * TX partial write safety:
 *   When HAL writes fewer bytes than drained from the TX ring buffer,
 *   the unwritten bytes are pushed back to the front of the ring buffer
 *   so no data is silently lost during backpressure.
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting. */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    board_uart_port_t port;
    uint32_t baudrate;

    /* RX: HAL UART → ring buffer */
    uint8_t             rx_storage[TRANSPORT_UART_RX_BUFFER_SIZE];
    byte_ring_buffer_t  rx_buffer;

    /* TX: ring buffer → HAL UART */
    uint8_t             tx_storage[TRANSPORT_UART_TX_BUFFER_SIZE];
    byte_ring_buffer_t  tx_buffer;

    /* Statistics */
    transport_uart_stats_t stats;
} transport_uart_t;

/* ---- API ---- */

/* Initialize UART transport.
 * Sets up HAL UART port, inits RX/TX ring buffers, zeros stats.
 * Configures service_step callback for the runtime component.
 *
 * IMPORTANT: hal_uart_init() must be called with a valid ops table
 * (e.g. hal_uart_stub_ops() for native tests) BEFORE calling this. */
hal_err_t transport_uart_init(transport_uart_t* uart, const transport_uart_config_t* config);

/* Reset transport UART: re-initialise buffers and stats.
 * Does NOT re-initialise the HAL port (caller must do that separately). */
hal_err_t transport_uart_reset(transport_uart_t* uart);

/* Service step: HAL RX → rx_buffer, tx_buffer → HAL TX.
 * Called by the runtime service loop. Non-blocking.
 *
 * TX partial write handling:
 *   1. Drain up to TRANSPORT_UART_BURST_SIZE bytes from TX ring buffer
 *   2. Write drained bytes to HAL UART
 *   3. If HAL wrote fewer bytes, push unwritten bytes back to ring buffer
 *   4. Increment tx_partial_writes and tx_pushback_bytes counters */
void transport_uart_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* Read bytes from RX buffer (consumed by upstream components).
 * Returns number of bytes read. */
size_t transport_uart_rx_read(transport_uart_t* uart, uint8_t* buf, size_t max_len);

/* Write bytes into TX buffer (produced by upstream components).
 * Returns number of bytes written (may be less than len if buffer is full). */
size_t transport_uart_tx_write(transport_uart_t* uart, const uint8_t* data, size_t len);

/* Bytes available in RX buffer. */
size_t transport_uart_rx_available(const transport_uart_t* uart);

/* Free space in TX buffer. */
size_t transport_uart_tx_free(const transport_uart_t* uart);

/* Get pointer to statistics (read-only access, valid as long as uart lives). */
const transport_uart_stats_t* transport_uart_get_stats(const transport_uart_t* uart);

/* Fill diagnostics struct. Returns HAL_OK on success. */
hal_err_t transport_uart_diagnostics(const transport_uart_t* uart, transport_uart_diagnostics_t* diag);

#ifdef __cplusplus
}
#endif
