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

/* ---- Transport TCP Config ---- */

#define TRANSPORT_TCP_RX_BUFFER_SIZE  512
#define TRANSPORT_TCP_TX_BUFFER_SIZE  512

typedef struct {
    uint32_t remote_ip;    /* IPv4 address in host byte order */
    uint16_t remote_port;
} transport_tcp_config_t;

/* ---- Transport TCP Diagnostics ---- */

typedef struct {
    uint32_t rx_total;          /* Cumulative bytes received */
    uint32_t tx_total;          /* Cumulative bytes transmitted */
    uint32_t rx_overflows;      /* RX buffer overflow count */
    uint32_t tx_backpressure;   /* TX partial write / failure events */
    uint32_t connect_count;     /* Total connect attempts */
    uint32_t disconnect_count;  /* Total disconnect events */
    size_t   rx_available;      /* Current bytes in RX buffer */
    size_t   tx_free;           /* Current free space in TX buffer */
} transport_tcp_diagnostics_t;

/* ---- Transport TCP HAL Ops ----
 *
 * Injectable ops for testing and platform abstraction.
 * Production implementation uses lwip sockets.
 * Tests inject fake implementations. */

typedef struct {
    hal_err_t (*connect)(uint32_t remote_ip, uint16_t remote_port);
    hal_err_t (*disconnect)(void);
    int (*recv)(uint8_t* buf, size_t max_len);     /* Returns bytes read (0 if none) */
    int (*send)(const uint8_t* buf, size_t len);   /* Returns bytes written (may be < len) */
    bool (*is_connected)(void);
} transport_tcp_hal_ops_t;

/* ---- Transport TCP Instance ----
 *
 * RX data flow: HAL TCP recv → rx_buffer → consumer reads via rx_read()
 * TX data flow: producer writes via tx_write() → tx_buffer → HAL TCP send
 *
 * This component contains NO NTRIP logic (no state machine, no RTCM routing).
 * It only provides raw TCP data via ring buffers.
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting. */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    uint32_t remote_ip;
    uint16_t remote_port;
    bool     connected;

    /* Statistics */
    uint32_t rx_total;
    uint32_t tx_total;
    uint32_t rx_overflows;
    uint32_t tx_backpressure;
    uint32_t connect_count;
    uint32_t disconnect_count;

    /* RX: HAL TCP recv → ring buffer */
    uint8_t             rx_storage[TRANSPORT_TCP_RX_BUFFER_SIZE];
    byte_ring_buffer_t  rx_buffer;

    /* TX: ring buffer → HAL TCP send */
    uint8_t             tx_storage[TRANSPORT_TCP_TX_BUFFER_SIZE];
    byte_ring_buffer_t  tx_buffer;

    /* HAL ops — set at init, NULL means use stubs */
    const transport_tcp_hal_ops_t* hal;
} transport_tcp_t;

/* ---- API ---- */

/* Initialize TCP transport with optional HAL ops (NULL = stubs).
 * Sets up RX/TX ring buffers and registers service_step callback. */
hal_err_t transport_tcp_init(transport_tcp_t* tcp, const transport_tcp_config_t* config);
hal_err_t transport_tcp_deinit(transport_tcp_t* tcp);

/* Set custom HAL ops (for testing or platform-specific backends).
 * Must be called before transport_tcp_connect(). */
void transport_tcp_set_hal_ops(transport_tcp_t* tcp, const transport_tcp_hal_ops_t* ops);

/* Connect to remote endpoint. Non-blocking.
 * Sets connected flag; real connect happens asynchronously. */
hal_err_t transport_tcp_connect(transport_tcp_t* tcp);

/* Disconnect from remote. Clears connected flag. */
hal_err_t transport_tcp_disconnect(transport_tcp_t* tcp);

/* Service step: HAL recv → rx_buffer, tx_buffer → HAL send.
 * Called by the runtime service loop. Non-blocking.
 * TX partial write safe: only consumes bytes actually sent. */
void transport_tcp_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* Read bytes from RX buffer. Returns bytes read. */
size_t transport_tcp_rx_read(transport_tcp_t* tcp, uint8_t* buf, size_t max_len);

/* Write bytes into TX buffer. Returns bytes written. */
size_t transport_tcp_tx_write(transport_tcp_t* tcp, const uint8_t* data, size_t len);

/* Bytes available in RX buffer. */
size_t transport_tcp_rx_available(const transport_tcp_t* tcp);

/* Free space in TX buffer. */
size_t transport_tcp_tx_free(const transport_tcp_t* tcp);

/* Check connection state. */
bool transport_tcp_is_connected(const transport_tcp_t* tcp);

/* Get full diagnostics snapshot. */
hal_err_t transport_tcp_get_diagnostics(const transport_tcp_t* tcp, transport_tcp_diagnostics_t* diag);

/* Reset transport: clear buffers, reset stats, preserve config. */
hal_err_t transport_tcp_reset(transport_tcp_t* tcp);

#ifdef __cplusplus
}
#endif
