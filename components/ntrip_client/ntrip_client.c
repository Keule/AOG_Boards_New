#include "ntrip_client.h"

#define NTRIP_RECONNECT_DELAY_CYCLES 50U

void ntrip_client_init(ntrip_client_t* client)
{
    if (client == 0) {
        return;
    }

    client->state = NTRIP_STATE_IDLE;
    client->connect_attempts = 0;
    client->reconnect_delay_cycles = NTRIP_RECONNECT_DELAY_CYCLES;
    client->retry_cycles_left = 0;
    client->bytes_received = 0;
    client->bytes_dropped = 0;
    client->last_error = 0;
    byte_ring_buffer_init(&client->rtcm_buffer, client->rtcm_storage, sizeof(client->rtcm_storage));
}

void ntrip_client_request_connect(ntrip_client_t* client)
{
    if (client == 0) {
        return;
    }

    client->state = NTRIP_STATE_CONNECTING;
    client->connect_attempts++;
}

void ntrip_client_service_step(ntrip_client_t* client)
{
    if (client == 0) {
        return;
    }

    switch (client->state) {
        case NTRIP_STATE_IDLE:
            break;
        case NTRIP_STATE_CONNECTING:
            client->state = NTRIP_STATE_AUTHENTICATING;
            break;
        case NTRIP_STATE_AUTHENTICATING:
            client->state = NTRIP_STATE_CONNECTED;
            break;
        case NTRIP_STATE_CONNECTED:
            client->state = NTRIP_STATE_STREAMING;
            break;
        case NTRIP_STATE_STREAMING:
            break;
        case NTRIP_STATE_RECONNECT_WAIT:
            if (client->retry_cycles_left > 0) {
                client->retry_cycles_left--;
            } else {
                client->state = NTRIP_STATE_CONNECTING;
                client->connect_attempts++;
            }
            break;
        case NTRIP_STATE_ERROR:
            client->state = NTRIP_STATE_RECONNECT_WAIT;
            client->retry_cycles_left = client->reconnect_delay_cycles;
            break;
        default:
            client->state = NTRIP_STATE_ERROR;
            break;
    }
}

void ntrip_client_report_error(ntrip_client_t* client, int32_t error_code)
{
    if (client == 0) {
        return;
    }

    client->last_error = error_code;
    client->state = NTRIP_STATE_ERROR;
}

size_t ntrip_client_feed_rtcm(ntrip_client_t* client, const uint8_t* data, size_t length)
{
    size_t pushed = 0;
    if (client == 0 || data == 0 || length == 0) {
        return 0;
    }

    if (client->state != NTRIP_STATE_STREAMING) {
        return 0;
    }

    pushed = byte_ring_buffer_push(&client->rtcm_buffer, data, length);
    client->bytes_received += (uint32_t)pushed;
    client->bytes_dropped += (uint32_t)(length - pushed);
    return pushed;
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

bool ntrip_client_is_streaming(const ntrip_client_t* client)
{
    return (client != 0) && (client->state == NTRIP_STATE_STREAMING);
}
