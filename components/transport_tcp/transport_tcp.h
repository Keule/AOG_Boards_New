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

typedef struct {
    uint32_t remote_ip;    /* IPv4 address in host byte order */
    uint16_t remote_port;
} transport_tcp_config_t;

/* ---- Transport TCP Instance ----
 *
 * RX data flow: HAL TCP → rx_buffer → consumer reads via rx_read()
 *
 * This component contains NO NTRIP logic (no state machine, no RTCM routing).
 * It only provides raw TCP data via a ring buffer.
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting. */
typedef struct {
    runtime_component_t component;    /* MUST be first */

    uint32_t remote_ip;
    uint16_t remote_port;
    bool     connected;

    /* RX: HAL TCP → ring buffer */
    uint8_t             rx_storage[TRANSPORT_TCP_RX_BUFFER_SIZE];
    byte_ring_buffer_t  rx_buffer;
} transport_tcp_t;

/* ---- API ---- */

hal_err_t transport_tcp_init(transport_tcp_t* tcp, const transport_tcp_config_t* config);
hal_err_t transport_tcp_deinit(transport_tcp_t* tcp);
hal_err_t transport_tcp_connect(transport_tcp_t* tcp);
hal_err_t transport_tcp_disconnect(transport_tcp_t* tcp);

/* Service step: HAL recv → rx_buffer.
 * Called by the runtime service loop. Non-blocking stub. */
void transport_tcp_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* Read bytes from RX buffer. */
size_t transport_tcp_rx_read(transport_tcp_t* tcp, uint8_t* buf, size_t max_len);

/* Bytes available in RX buffer. */
size_t transport_tcp_rx_available(const transport_tcp_t* tcp);

/* Check connection state. */
bool transport_tcp_is_connected(const transport_tcp_t* tcp);

#ifdef __cplusplus
}
#endif
