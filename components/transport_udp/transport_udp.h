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

/* ---- Transport UDP Instance ----
 *
 * RX data flow: HAL UDP → rx_buffer → consumer reads via rx_read()
 * TX data flow: producer writes via tx_write() → tx_buffer → HAL UDP
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

    /* RX: HAL UDP → ring buffer */
    uint8_t             rx_storage[TRANSPORT_UDP_RX_BUFFER_SIZE];
    byte_ring_buffer_t  rx_buffer;

    /* TX: ring buffer → HAL UDP */
    uint8_t             tx_storage[TRANSPORT_UDP_TX_BUFFER_SIZE];
    byte_ring_buffer_t  tx_buffer;
} transport_udp_t;

/* ---- API ---- */

hal_err_t transport_udp_init(transport_udp_t* udp, const transport_udp_config_t* config);
hal_err_t transport_udp_deinit(transport_udp_t* udp);

/* Service step: HAL recv → rx_buffer, tx_buffer → HAL send.
 * Called by the runtime service loop. Non-blocking stub. */
void transport_udp_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* Read bytes from RX buffer. */
size_t transport_udp_rx_read(transport_udp_t* udp, uint8_t* buf, size_t max_len);

/* Write bytes into TX buffer. */
size_t transport_udp_tx_write(transport_udp_t* udp, const uint8_t* data, size_t len);

/* Bytes available in RX buffer. */
size_t transport_udp_rx_available(const transport_udp_t* udp);

/* Free space in TX buffer. */
size_t transport_udp_tx_free(const transport_udp_t* udp);

/* Check if socket is bound. */
bool transport_udp_is_bound(const transport_udp_t* udp);

#ifdef __cplusplus
}
#endif
