#include "transport_tcp.h"

void transport_tcp_init(transport_tcp_t* transport)
{
    if (transport == 0) {
        return;
    }

    byte_ring_buffer_init(&transport->rx_raw, transport->rx_storage, sizeof(transport->rx_storage));
    byte_ring_buffer_init(&transport->tx_raw, transport->tx_storage, sizeof(transport->tx_storage));
}

size_t transport_tcp_feed_rx_raw(transport_tcp_t* transport, const uint8_t* data, size_t length)
{
    return (transport != 0) ? byte_ring_buffer_push(&transport->rx_raw, data, length) : 0;
}

size_t transport_tcp_pop_rx_raw(transport_tcp_t* transport, uint8_t* out_data, size_t max_length)
{
    return (transport != 0) ? byte_ring_buffer_pop(&transport->rx_raw, out_data, max_length) : 0;
}

size_t transport_tcp_push_tx_raw(transport_tcp_t* transport, const uint8_t* data, size_t length)
{
    return (transport != 0) ? byte_ring_buffer_push(&transport->tx_raw, data, length) : 0;
}

size_t transport_tcp_drain_tx_raw(transport_tcp_t* transport, uint8_t* out_data, size_t max_length)
{
    return (transport != 0) ? byte_ring_buffer_pop(&transport->tx_raw, out_data, max_length) : 0;
}

void transport_tcp_service_step(transport_tcp_t* transport)
{
    (void)transport;
}
