#pragma once

#include <stddef.h>
#include <stdint.h>

#include "byte_ring_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    byte_ring_buffer_t* rx_buffer;
    byte_ring_buffer_t* tx_buffer;
} transport_uart_channel_t;

void transport_uart_bind(transport_uart_channel_t* channel, byte_ring_buffer_t* rx_buffer, byte_ring_buffer_t* tx_buffer);
size_t transport_uart_feed_rx(transport_uart_channel_t* channel, const uint8_t* data, size_t length);
size_t transport_uart_drain_tx(transport_uart_channel_t* channel, uint8_t* out_data, size_t max_length);
void transport_uart_service_step(transport_uart_channel_t* channels, size_t channel_count);

#ifdef __cplusplus
}
#endif
