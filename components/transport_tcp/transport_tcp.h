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

#define TRANSPORT_TCP_RX_BUFFER_SIZE  4096
#define TRANSPORT_TCP_TX_BUFFER_SIZE  512
#define TRANSPORT_TCP_HOSTNAME_MAX    128

/* ---- Transport TCP Sub-States ---- */

typedef enum {
    TCP_SUB_IDLE = 0,         /* Not connected, not attempting */
    TCP_SUB_DNS_RESOLVING,    /* DNS lookup in progress */
    TCP_SUB_TCP_CONNECTING,   /* TCP connect() in progress */
    TCP_SUB_CONNECTED,        /* Fully connected */
    TCP_SUB_ERROR,            /* Error state (auto-clears on next connect) */
} tcp_sub_state_t;

/* ---- Transport TCP Config ---- */

typedef struct {
    char     hostname[TRANSPORT_TCP_HOSTNAME_MAX];  /* Hostname for DNS resolution */
    uint16_t remote_port;
} transport_tcp_config_t;

/* ---- Transport TCP Instance ----
 *
 * RX data flow: lwip socket (nonblocking) → rx_buffer → consumer reads via rx_read()
 * TX data flow: producer writes via tx_write() → tx_buffer → lwip send() (nonblocking)
 *
 * This component uses REAL lwip sockets with nonblocking mode.
 * DNS resolution is done asynchronously via getaddrinfo_a() or
 * synchronous with a short timeout to avoid blocking.
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting. */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    /* Configuration (copied from init) */
    char     hostname[TRANSPORT_TCP_HOSTNAME_MAX];
    uint16_t remote_port;
    uint32_t resolved_ip;              /* 0 = not yet resolved */

    /* Connection state */
    bool            connected;
    tcp_sub_state_t sub_state;
    int             sock_fd;           /* lwip socket, -1 = not created */

    /* Error tracking */
    int             last_errno;        /* Last socket errno */
    uint32_t        connect_start_ms;  /* Timestamp when connect was initiated */
    uint32_t        total_rx_bytes;
    uint32_t        total_tx_bytes;
    uint32_t        tcp_rx_drops;     /* cumulative RX bytes dropped (buffer full) */

    /* RX: lwip recv → ring buffer */
    uint8_t             rx_storage[TRANSPORT_TCP_RX_BUFFER_SIZE];
    byte_ring_buffer_t  rx_buffer;

    /* TX: ring buffer → lwip send */
    uint8_t             tx_storage[TRANSPORT_TCP_TX_BUFFER_SIZE];
    byte_ring_buffer_t  tx_buffer;
} transport_tcp_t;

/* ---- API ---- */

hal_err_t transport_tcp_init(transport_tcp_t* tcp, const transport_tcp_config_t* config);
hal_err_t transport_tcp_deinit(transport_tcp_t* tcp);

/* Initiate nonblocking connect sequence (DNS + TCP).
 * Sets sub_state to DNS_RESOLVING. Actual work happens in service_step. */
hal_err_t transport_tcp_connect(transport_tcp_t* tcp);

/* Close socket and reset state to IDLE. */
hal_err_t transport_tcp_disconnect(transport_tcp_t* tcp);

/* Service step: DNS resolve, TCP connect, TX drain → lwip, lwip recv → rx_buffer.
 * Called by the runtime service loop. NON-BLOCKING. */
void transport_tcp_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* Write bytes into TX buffer. Returns number of bytes accepted (may be < len). */
size_t transport_tcp_tx_write(transport_tcp_t* tcp, const uint8_t* data, size_t len);

/* Read bytes from RX buffer. Returns number of bytes read. */
size_t transport_tcp_rx_read(transport_tcp_t* tcp, uint8_t* buf, size_t max_len);

/* Bytes available in RX buffer. */
size_t transport_tcp_rx_available(const transport_tcp_t* tcp);

/* Bytes pending in TX buffer. */
size_t transport_tcp_tx_available(const transport_tcp_t* tcp);

/* Free space in TX buffer. */
size_t transport_tcp_tx_free(const transport_tcp_t* tcp);

/* Check connection state. */
bool transport_tcp_is_connected(const transport_tcp_t* tcp);

/* Get current sub-state for diagnostics. */
tcp_sub_state_t transport_tcp_get_sub_state(const transport_tcp_t* tcp);

/* Get last socket errno. */
int transport_tcp_get_last_errno(const transport_tcp_t* tcp);

/* Get total RX/TX bytes. */
uint32_t transport_tcp_get_total_rx(const transport_tcp_t* tcp);
uint32_t transport_tcp_get_total_tx(const transport_tcp_t* tcp);

/* Get cumulative RX drop count (bytes lost due to RX buffer full). */
uint32_t transport_tcp_get_rx_drops(const transport_tcp_t* tcp);

/* Get sub-state name string (for diagnostics). */
const char* transport_tcp_sub_state_name(tcp_sub_state_t state);

#ifdef __cplusplus
}
#endif
