#include "ntrip_client.h"
#include <string.h>

/* ---- State name ---- */

const char* ntrip_client_state_name(ntrip_state_t state)
{
    switch (state) {
    case NTRIP_STATE_IDLE:           return "idle";
    case NTRIP_STATE_CONNECTING:     return "connecting";
    case NTRIP_STATE_AUTHENTICATING: return "authenticating";
    case NTRIP_STATE_CONNECTED:      return "connected";
    case NTRIP_STATE_ERROR:          return "error";
    case NTRIP_STATE_RECONNECT:      return "reconnect";
    default:                         return "unknown";
    }
}

/* ---- Valid state transitions ---- */

static bool ntrip_is_valid_transition(ntrip_state_t from, ntrip_state_t to)
{
    if (from == to) {
        return true;
    }

    switch (from) {
    case NTRIP_STATE_IDLE:
        return to == NTRIP_STATE_CONNECTING;
    case NTRIP_STATE_CONNECTING:
        return to == NTRIP_STATE_AUTHENTICATING || to == NTRIP_STATE_ERROR;
    case NTRIP_STATE_AUTHENTICATING:
        return to == NTRIP_STATE_CONNECTED || to == NTRIP_STATE_ERROR;
    case NTRIP_STATE_CONNECTED:
        return to == NTRIP_STATE_ERROR || to == NTRIP_STATE_IDLE;
    case NTRIP_STATE_ERROR:
        return to == NTRIP_STATE_RECONNECT || to == NTRIP_STATE_IDLE;
    case NTRIP_STATE_RECONNECT:
        return to == NTRIP_STATE_CONNECTING || to == NTRIP_STATE_IDLE;
    default:
        return false;
    }
}

/* ---- Public API ---- */

void ntrip_client_init(ntrip_client_t* client)
{
    if (client == NULL) {
        return;
    }
    memset(client, 0, sizeof(ntrip_client_t));
    client->state = NTRIP_STATE_IDLE;
    client->started = false;
    byte_ring_buffer_init(&client->rtcm_buffer,
                          client->rtcm_storage,
                          sizeof(client->rtcm_storage));
    client->component.service_step = ntrip_client_service_step;
}

bool ntrip_client_start(ntrip_client_t* client)
{
    if (client == NULL) {
        return false;
    }
    if (client->state != NTRIP_STATE_IDLE) {
        return false;
    }
    /* Use 0 as timestamp — will be corrected on first service_step call */
    client->started = true;
    return ntrip_client_transition(client, NTRIP_STATE_CONNECTING, 0);
}

bool ntrip_client_is_started(const ntrip_client_t* client)
{
    if (client == NULL) {
        return false;
    }
    return client->started;
}

void ntrip_client_set_tcp_source(ntrip_client_t* client, byte_ring_buffer_t* source)
{
    if (client == NULL) {
        return;
    }
    client->tcp_source = source;
}

bool ntrip_client_transition(ntrip_client_t* client, ntrip_state_t new_state, uint64_t timestamp_us)
{
    if (client == NULL) {
        return false;
    }

    if (!ntrip_is_valid_transition(client->state, new_state)) {
        return false;
    }

    client->state = new_state;
    client->last_state_change_us = timestamp_us;

    if (new_state == NTRIP_STATE_RECONNECT) {
        client->reconnect_count++;
    }

    if (new_state == NTRIP_STATE_CONNECTED) {
        client->reconnect_count = 0;
    }

    /* Clear RTCM buffer when leaving connected state */
    if (new_state != NTRIP_STATE_CONNECTED) {
        client->rtcm_buffer.head = 0;
        client->rtcm_buffer.tail = 0;
        client->rtcm_buffer.size = 0;
        client->rtcm_buffer.overflow_count = 0;
    }

    return true;
}

ntrip_state_t ntrip_client_get_state(const ntrip_client_t* client)
{
    if (client == NULL) {
        return NTRIP_STATE_IDLE;
    }
    return client->state;
}

uint32_t ntrip_client_get_reconnect_count(const ntrip_client_t* client)
{
    if (client == NULL) {
        return 0;
    }
    return client->reconnect_count;
}

/* ---- Service step ---- */

void ntrip_client_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    ntrip_client_t* client = (ntrip_client_t*)comp;
    if (client == NULL || !client->started) {
        return;
    }

    /* Fix initial timestamp (ntrip_client_start uses 0) */
    if (client->last_state_change_us == 0 && timestamp_us > 0) {
        client->last_state_change_us = timestamp_us;
    }

    uint64_t elapsed = timestamp_us - client->last_state_change_us;

    /* ---- Skeleton state progression (simulated TCP handshake) ---- */
    switch (client->state) {
    case NTRIP_STATE_CONNECTING:
        /* Simulate TCP connect timeout */
        if (elapsed >= NTRIP_SKELETON_CONNECTING_TIMEOUT_US) {
            ntrip_client_transition(client, NTRIP_STATE_AUTHENTICATING, timestamp_us);
        }
        break;

    case NTRIP_STATE_AUTHENTICATING:
        /* Simulate NTRIP auth response timeout */
        if (elapsed >= NTRIP_SKELETON_AUTHENTICATING_TIMEOUT_US) {
            ntrip_client_transition(client, NTRIP_STATE_CONNECTED, timestamp_us);
        }
        break;

    case NTRIP_STATE_CONNECTED:
        /* Forward RTCM data from TCP source to RTCM output buffer */
        if (client->tcp_source == NULL) {
            break;
        }

        {
            uint8_t tmp[128];
            size_t available = byte_ring_buffer_available(client->tcp_source);
            if (available > 0) {
                size_t to_read = available > sizeof(tmp) ? sizeof(tmp) : available;
                size_t pulled = byte_ring_buffer_read(client->tcp_source, tmp, to_read);
                if (pulled > 0) {
                    byte_ring_buffer_write(&client->rtcm_buffer, tmp, pulled);
                }
            }
        }
        break;

    case NTRIP_STATE_ERROR:
        /* Skeleton: auto-retry after error (with backoff) */
        if (elapsed >= NTRIP_SKELETON_AUTHENTICATING_TIMEOUT_US) {
            ntrip_client_transition(client, NTRIP_STATE_RECONNECT, timestamp_us);
        }
        break;

    case NTRIP_STATE_RECONNECT:
        /* Skeleton: immediately retry */
        ntrip_client_transition(client, NTRIP_STATE_CONNECTING, timestamp_us);
        break;

    case NTRIP_STATE_IDLE:
    default:
        break;
    }
}

/* ---- RTCM Data API ---- */

size_t ntrip_client_pop_rtcm(ntrip_client_t* client, uint8_t* buf, size_t max_len)
{
    if (client == NULL || buf == NULL || max_len == 0) {
        return 0;
    }
    return byte_ring_buffer_read(&client->rtcm_buffer, buf, max_len);
}

size_t ntrip_client_rtcm_available(const ntrip_client_t* client)
{
    if (client == NULL) {
        return 0;
    }
    return byte_ring_buffer_available(&client->rtcm_buffer);
}
