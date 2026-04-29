#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "runtime_component.h"
#include "byte_ring_buffer.h"
#include "hal_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Transport UDP Config ---- */

#define TRANSPORT_UDP_RX_BUFFER_SIZE  512
#define TRANSPORT_UDP_TX_BUFFER_SIZE  512

typedef struct {
    uint16_t local_port;
    uint32_t remote_ip;    /* IPv4 address in host byte order */
    uint16_t remote_port;
} transport_udp_config_t;

/* ---- Transport UDP Diagnostics ---- */

typedef struct {
    uint32_t rx_total;          /* Cumulative bytes received */
    uint32_t tx_total;          /* Cumulative bytes transmitted */
    uint32_t rx_overflows;      /* RX buffer overflow count */
    uint32_t tx_backpressure;   /* TX partial write / failure events */
    uint32_t packets_rx;        /* Number of datagrams received */
    uint32_t packets_tx;        /* Number of datagrams sent */
    size_t   rx_available;      /* Current bytes in RX buffer */
    size_t   tx_free;           /* Current free space in TX buffer */
} transport_udp_diagnostics_t;

/* ---- Transport UDP HAL Ops ----
 *
 * Injectable ops for testing and platform abstraction.
 * Production implementation uses lwip sockets.
 * Tests inject fake implementations. */

typedef struct {
    hal_err_t (*bind)(uint16_t local_port);
    hal_err_t (*close)(void);
    int (*recvfrom)(uint8_t* buf, size_t max_len, uint32_t* src_ip, uint16_t* src_port);
    int (*sendto)(const uint8_t* buf, size_t len, uint32_t dst_ip, uint16_t dst_port);
    bool (*is_bound)(void);
} transport_udp_hal_ops_t;

/* ---- Transport UDP Instance ----
 *
 * RX data flow: HAL UDP recvfrom → rx_buffer → consumer reads via rx_read()
 * TX data flow: producer writes via tx_write() → tx_buffer → HAL UDP sendto
 *
 * This component contains NO AOG logic (no PGN parsing, no Hello handling).
 * It only transports bytes between HAL and ring buffers.
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting. */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    bool     bound;

    /* Statistics */
    uint32_t rx_total;
    uint32_t tx_total;
    uint32_t rx_overflows;
    uint32_t tx_backpressure;
    uint32_t packets_rx;
    uint32_t packets_tx;

    /* RX: HAL UDP recvfrom → ring buffer */
    uint8_t             rx_storage[TRANSPORT_UDP_RX_BUFFER_SIZE];
    byte_ring_buffer_t  rx_buffer;

    /* TX: ring buffer → HAL UDP sendto */
    uint8_t             tx_storage[TRANSPORT_UDP_TX_BUFFER_SIZE];
    byte_ring_buffer_t  tx_buffer;

    /* HAL ops — set at init, NULL means use stubs */
    const transport_udp_hal_ops_t* hal;
} transport_udp_t;

/* ---- API ---- */

/* Initialize UDP transport with optional HAL ops (NULL = stubs).
 * Sets up RX/TX ring buffers and binds local port. */
hal_err_t transport_udp_init(transport_udp_t* udp, const transport_udp_config_t* config);
hal_err_t transport_udp_deinit(transport_udp_t* udp);

/* Set custom HAL ops (for testing or platform-specific backends).
 * Must be called before transport_udp_init(). */
void transport_udp_set_hal_ops(transport_udp_t* udp, const transport_udp_hal_ops_t* ops);

/* Service step: HAL recvfrom → rx_buffer, tx_buffer → HAL sendto.
 * Called by the runtime service loop. Non-blocking.
 * TX partial write safe: only consumes bytes actually sent. */
void transport_udp_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* Read bytes from RX buffer. Returns bytes read. */
size_t transport_udp_rx_read(transport_udp_t* udp, uint8_t* buf, size_t max_len);

/* Write bytes into TX buffer. Returns bytes written. */
size_t transport_udp_tx_write(transport_udp_t* udp, const uint8_t* data, size_t len);

/* Bytes available in RX buffer. */
size_t transport_udp_rx_available(const transport_udp_t* udp);

/* Free space in TX buffer. */
size_t transport_udp_tx_free(const transport_udp_t* udp);

/* Check if socket is bound. */
bool transport_udp_is_bound(const transport_udp_t* udp);

/* Get full diagnostics snapshot. */
hal_err_t transport_udp_get_diagnostics(const transport_udp_t* udp, transport_udp_diagnostics_t* diag);

/* Reset transport: clear buffers, reset stats, preserve config. */
hal_err_t transport_udp_reset(transport_udp_t* udp);

#ifdef __cplusplus
}
#endif
