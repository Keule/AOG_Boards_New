#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "message_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t payload[64];
    uint16_t length;
} transport_udp_packet_t;

typedef struct {
    message_queue_t rx_queue;
    message_queue_t tx_queue;
    transport_udp_packet_t rx_storage[8];
    transport_udp_packet_t tx_storage[8];
} transport_udp_t;

void transport_udp_init(transport_udp_t* transport);
bool transport_udp_feed_rx(transport_udp_t* transport, const transport_udp_packet_t* packet);
bool transport_udp_pop_rx(transport_udp_t* transport, transport_udp_packet_t* out_packet);
bool transport_udp_push_tx(transport_udp_t* transport, const transport_udp_packet_t* packet);
bool transport_udp_drain_tx(transport_udp_t* transport, transport_udp_packet_t* out_packet);
void transport_udp_service_step(transport_udp_t* transport);

#ifdef __cplusplus
}
#endif
