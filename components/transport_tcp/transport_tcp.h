#pragma once

#include <stddef.h>
#include <stdint.h>

#include "byte_ring_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    byte_ring_buffer_t rx_raw;
    byte_ring_buffer_t tx_raw;
    uint8_t rx_storage[512];
    uint8_t tx_storage[512];
} transport_tcp_t;

void transport_tcp_init(transport_tcp_t* transport);
size_t transport_tcp_feed_rx_raw(transport_tcp_t* transport, const uint8_t* data, size_t length);
size_t transport_tcp_pop_rx_raw(transport_tcp_t* transport, uint8_t* out_data, size_t max_length);
size_t transport_tcp_push_tx_raw(transport_tcp_t* transport, const uint8_t* data, size_t length);
size_t transport_tcp_drain_tx_raw(transport_tcp_t* transport, uint8_t* out_data, size_t max_length);
void transport_tcp_service_step(transport_tcp_t* transport);

#ifdef __cplusplus
}
#endif
