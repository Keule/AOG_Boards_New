#include "transport_uart.h"

void transport_uart_bind(transport_uart_channel_t* channel, byte_ring_buffer_t* rx_buffer, byte_ring_buffer_t* tx_buffer)
{
    if (channel == 0) {
        return;
    }

    channel->rx_buffer = rx_buffer;
    channel->tx_buffer = tx_buffer;
}

size_t transport_uart_feed_rx(transport_uart_channel_t* channel, const uint8_t* data, size_t length)
{
    if (channel == 0 || channel->rx_buffer == 0) {
        return 0;
    }

    return byte_ring_buffer_push(channel->rx_buffer, data, length);
}

size_t transport_uart_drain_tx(transport_uart_channel_t* channel, uint8_t* out_data, size_t max_length)
{
    if (channel == 0 || channel->tx_buffer == 0) {
        return 0;
    }

    return byte_ring_buffer_pop(channel->tx_buffer, out_data, max_length);
}

void transport_uart_service_step(transport_uart_channel_t* channels, size_t channel_count)
{
    (void)channels;
    (void)channel_count;
}
