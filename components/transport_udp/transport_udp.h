#pragma once

#include <stdbool.h>

#include "aog_navigation_app.h"
#include "message_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    message_queue_t rx_queue;
    message_queue_t tx_queue;
    aog_frame_t rx_storage[8];
    aog_frame_t tx_storage[8];
} transport_udp_t;

void transport_udp_init(transport_udp_t* transport);
bool transport_udp_feed_rx(transport_udp_t* transport, const aog_frame_t* frame);
bool transport_udp_pop_rx(transport_udp_t* transport, aog_frame_t* out_frame);
bool transport_udp_push_tx(transport_udp_t* transport, const aog_frame_t* frame);
bool transport_udp_drain_tx(transport_udp_t* transport, aog_frame_t* out_frame);
void transport_udp_service_step(transport_udp_t* transport);

#ifdef __cplusplus
}
#endif
