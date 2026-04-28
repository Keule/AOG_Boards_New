#include "ntrip_client.h"

#include <string.h>

void ntrip_client_init(ntrip_client_t* client)
{
    if (client == 0) {
        return;
    }

    client->state = NTRIP_STATE_IDLE;
    client->reconnect_attempts = 0;
    client->last_error = 0;
    client->rtcm_length = 0;
    memset(client->rtcm_buffer, 0, sizeof(client->rtcm_buffer));
}

void ntrip_client_request_connect(ntrip_client_t* client)
{
    if (client == 0) {
        return;
    }

    client->state = NTRIP_STATE_CONNECTING;
}

void ntrip_client_on_connected(ntrip_client_t* client)
{
    if (client == 0) {
        return;
    }

    client->state = NTRIP_STATE_AUTHENTICATING;
}

void ntrip_client_on_authenticated(ntrip_client_t* client)
{
    if (client == 0) {
        return;
    }

    client->state = NTRIP_STATE_CONNECTED;
}

void ntrip_client_on_error(ntrip_client_t* client, int32_t error_code)
{
    if (client == 0) {
        return;
    }

    client->state = NTRIP_STATE_ERROR_RECONNECT;
    client->last_error = error_code;
    client->reconnect_attempts++;
}

void ntrip_client_step(ntrip_client_t* client)
{
    static const uint8_t k_stub_rtcm[] = {0xD3, 0x00, 0x00};

    if (client == 0) {
        return;
    }

    if (client->state == NTRIP_STATE_ERROR_RECONNECT) {
        client->state = NTRIP_STATE_CONNECTING;
        return;
    }

    if (client->state == NTRIP_STATE_CONNECTING) {
        client->state = NTRIP_STATE_AUTHENTICATING;
        return;
    }

    if (client->state == NTRIP_STATE_AUTHENTICATING) {
        client->state = NTRIP_STATE_CONNECTED;
        return;
    }

    if (client->state == NTRIP_STATE_CONNECTED && client->rtcm_length == 0) {
        memcpy(client->rtcm_buffer, k_stub_rtcm, sizeof(k_stub_rtcm));
        client->rtcm_length = sizeof(k_stub_rtcm);
    }
}

int ntrip_client_pop_rtcm(ntrip_client_t* client, uint8_t* out_data, size_t max_length, size_t* out_length)
{
    size_t copy_len = 0;

    if (client == 0 || out_data == 0 || out_length == 0) {
        return -1;
    }

    if (client->rtcm_length == 0) {
        *out_length = 0;
        return 0;
    }

    copy_len = (client->rtcm_length <= max_length) ? client->rtcm_length : max_length;
    memcpy(out_data, client->rtcm_buffer, copy_len);
    client->rtcm_length = 0;
    *out_length = copy_len;

    return 0;
}

int ntrip_client_mock_push_rtcm(ntrip_client_t* client, const uint8_t* data, size_t length)
{
    if (client == 0 || data == 0 || length == 0 || length > sizeof(client->rtcm_buffer)) {
        return -1;
    }

    memcpy(client->rtcm_buffer, data, length);
    client->rtcm_length = length;
    return 0;
}
