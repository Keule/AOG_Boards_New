#include "ntrip_client.h"

void ntrip_client_init(ntrip_client_t* client)
{
    if (client == 0) {
        return;
    }

    client->state = NTRIP_STATE_IDLE;
    byte_ring_buffer_init(&client->rtcm_buffer, client->rtcm_storage, sizeof(client->rtcm_storage));
}

void ntrip_client_request_connect(ntrip_client_t* client)
{
    if (client == 0) {
        return;
    }

    client->state = NTRIP_STATE_CONNECTING;
}

void ntrip_client_service_step(ntrip_client_t* client)
{
    if (client == 0) {
        return;
    }

    if (client->state == NTRIP_STATE_CONNECTING) {
        client->state = NTRIP_STATE_AUTHENTICATING;
    } else if (client->state == NTRIP_STATE_AUTHENTICATING) {
        client->state = NTRIP_STATE_STREAMING;
    }
}

size_t ntrip_client_feed_rtcm(ntrip_client_t* client, const uint8_t* data, size_t length)
{
    if (client == 0) {
        return 0;
    }

    return byte_ring_buffer_push(&client->rtcm_buffer, data, length);
}

size_t ntrip_client_pop_rtcm(ntrip_client_t* client, uint8_t* out_data, size_t max_length)
{
    if (client == 0) {
        return 0;
    }

    return byte_ring_buffer_pop(&client->rtcm_buffer, out_data, max_length);
}

size_t ntrip_client_peek_rtcm(const ntrip_client_t* client, uint8_t* out_data, size_t max_length)
{
    if (client == 0) {
        return 0;
    }

    return byte_ring_buffer_peek(&client->rtcm_buffer, out_data, max_length);
}
