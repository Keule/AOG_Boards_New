#include "ntrip_client.h"

void ntrip_client_init(ntrip_client_t* client)
{
    if (client == 0) {
        return;
    }

    client->state = NTRIP_STATE_IDLE;
    client->reconnect_attempts = 0;
    client->last_error = 0;
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
    if (client == 0) {
        return;
    }

    if (client->state == NTRIP_STATE_ERROR_RECONNECT) {
        client->state = NTRIP_STATE_CONNECTING;
    }
}
