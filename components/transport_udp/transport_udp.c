#include "transport_udp.h"

void transport_udp_init(transport_udp_t* transport)
{
    if (transport == 0) {
        return;
    }

    message_queue_init(&transport->rx_queue, transport->rx_storage, sizeof(aog_frame_t), 8);
    message_queue_init(&transport->tx_queue, transport->tx_storage, sizeof(aog_frame_t), 8);
}

bool transport_udp_feed_rx(transport_udp_t* transport, const aog_frame_t* frame)
{
    return (transport != 0 && frame != 0) ? message_queue_push(&transport->rx_queue, frame) : false;
}

bool transport_udp_pop_rx(transport_udp_t* transport, aog_frame_t* out_frame)
{
    return (transport != 0 && out_frame != 0) ? message_queue_pop(&transport->rx_queue, out_frame) : false;
}

bool transport_udp_push_tx(transport_udp_t* transport, const aog_frame_t* frame)
{
    return (transport != 0 && frame != 0) ? message_queue_push(&transport->tx_queue, frame) : false;
}

bool transport_udp_drain_tx(transport_udp_t* transport, aog_frame_t* out_frame)
{
    return (transport != 0 && out_frame != 0) ? message_queue_pop(&transport->tx_queue, out_frame) : false;
}

void transport_udp_service_step(transport_udp_t* transport)
{
    (void)transport;
}
