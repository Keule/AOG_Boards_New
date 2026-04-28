#include "transport_udp.h"

void transport_udp_init(transport_udp_t* transport)
{
    if (transport == 0) {
        return;
    }

    message_queue_init(&transport->rx_queue, transport->rx_storage, sizeof(transport_udp_packet_t), 8);
    message_queue_init(&transport->tx_queue, transport->tx_storage, sizeof(transport_udp_packet_t), 8);
}

bool transport_udp_feed_rx(transport_udp_t* transport, const transport_udp_packet_t* packet)
{
    return (transport != 0 && packet != 0) ? message_queue_push(&transport->rx_queue, packet) : false;
}

bool transport_udp_pop_rx(transport_udp_t* transport, transport_udp_packet_t* out_packet)
{
    return (transport != 0 && out_packet != 0) ? message_queue_pop(&transport->rx_queue, out_packet) : false;
}

bool transport_udp_push_tx(transport_udp_t* transport, const transport_udp_packet_t* packet)
{
    return (transport != 0 && packet != 0) ? message_queue_push(&transport->tx_queue, packet) : false;
}

bool transport_udp_drain_tx(transport_udp_t* transport, transport_udp_packet_t* out_packet)
{
    return (transport != 0 && out_packet != 0) ? message_queue_pop(&transport->tx_queue, out_packet) : false;
}

void transport_udp_service_step(transport_udp_t* transport)
{
    (void)transport;
}
