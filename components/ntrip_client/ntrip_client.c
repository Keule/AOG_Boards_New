#include "ntrip_client.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ---- State name ---- */

const char* ntrip_client_state_name(ntrip_state_t state)
{
    switch (state) {
    case NTRIP_STATE_IDLE:           return "idle";
    case NTRIP_STATE_CONNECTING:     return "connecting";
    case NTRIP_STATE_BUILD_REQUEST:  return "build_request";
    case NTRIP_STATE_SEND_REQUEST:   return "send_request";
    case NTRIP_STATE_WAIT_RESPONSE:  return "wait_response";
    case NTRIP_STATE_CONNECTED:      return "connected";
    case NTRIP_STATE_ERROR:          return "error";
    case NTRIP_STATE_RETRY_WAIT:     return "retry_wait";
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
        return to == NTRIP_STATE_BUILD_REQUEST || to == NTRIP_STATE_ERROR;
    case NTRIP_STATE_BUILD_REQUEST:
        return to == NTRIP_STATE_SEND_REQUEST || to == NTRIP_STATE_ERROR;
    case NTRIP_STATE_SEND_REQUEST:
        return to == NTRIP_STATE_WAIT_RESPONSE || to == NTRIP_STATE_ERROR;
    case NTRIP_STATE_WAIT_RESPONSE:
        return to == NTRIP_STATE_CONNECTED || to == NTRIP_STATE_ERROR;
    case NTRIP_STATE_CONNECTED:
        return to == NTRIP_STATE_ERROR || to == NTRIP_STATE_IDLE;
    case NTRIP_STATE_ERROR:
        return to == NTRIP_STATE_RETRY_WAIT || to == NTRIP_STATE_IDLE;
    case NTRIP_STATE_RETRY_WAIT:
        return to == NTRIP_STATE_CONNECTING || to == NTRIP_STATE_IDLE;
    default:
        return false;
    }
}

/* ---- Base64 encoder (minimal, no external deps) ---- */

static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t base64_encode(const uint8_t* src, size_t src_len,
                            char* dst, size_t dst_max)
{
    size_t out = 0;
    size_t i;
    for (i = 0; i + 2 < src_len; i += 3) {
        if (out + 4 >= dst_max) return 0;
        uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i+1] << 8) | src[i+2];
        dst[out++] = base64_table[(v >> 18) & 0x3F];
        dst[out++] = base64_table[(v >> 12) & 0x3F];
        dst[out++] = base64_table[(v >>  6) & 0x3F];
        dst[out++] = base64_table[ v        & 0x3F];
    }
    if (i < src_len) {
        uint32_t v = (uint32_t)src[i] << 16;
        if (i + 1 < src_len) v |= (uint32_t)src[i+1] << 8;
        if (out + 4 >= dst_max) return 0;
        dst[out++] = base64_table[(v >> 18) & 0x3F];
        dst[out++] = base64_table[(v >> 12) & 0x3F];
        dst[out++] = (i + 1 < src_len) ? base64_table[(v >> 6) & 0x3F] : '=';
        dst[out++] = '=';
    }
    if (out >= dst_max) return 0;
    dst[out] = '\0';
    return out;
}

/* ---- Request builder (bounds-safe) ---- */

typedef struct {
    char*  buf;
    size_t pos;
    size_t capacity;
    bool   overflow;
} request_builder_t;

static void rb_init(request_builder_t* rb, char* buf, size_t capacity)
{
    rb->buf = buf;
    rb->pos = 0;
    rb->capacity = capacity;
    rb->overflow = false;
}

static void rb_append(request_builder_t* rb, const char* str)
{
    if (rb->overflow || str == NULL) return;
    size_t len = strlen(str);
    if (rb->pos + len >= rb->capacity) {
        rb->overflow = true;
        return;
    }
    memcpy(rb->buf + rb->pos, str, len);
    rb->pos += len;
}

static void rb_printf(request_builder_t* rb, const char* fmt, ...)
{
    if (rb->overflow) return;
    size_t remaining = rb->capacity - rb->pos;
    if (remaining < 2) {
        rb->overflow = true;
        return;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(rb->buf + rb->pos, remaining, fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= remaining) {
        rb->overflow = true;
        return;
    }
    rb->pos += (size_t)written;
}

/* ---- Build NTRIP HTTP GET request ---- */

static ntrip_err_t ntrip_build_request(ntrip_client_t* client)
{
    request_builder_t rb;
    rb_init(&rb, (char*)client->request_buf, sizeof(client->request_buf));

    rb_printf(&rb, "GET /%s HTTP/1.1\r\n", client->config.mountpoint);
    rb_printf(&rb, "Host: %s:%u\r\n", client->config.host,
              (unsigned)client->config.port);
    rb_append(&rb, "Ntrip-Version: Ntrip/2.0\r\n");
    rb_printf(&rb, "User-Agent: %s\r\n", client->config.user_agent);

    /* Authorization: Basic base64(user:pass) — only if credentials provided */
    if (client->config.username[0] != '\0') {
        char cred_plain[2 * NTRIP_MAX_CRED_LEN + 2];
        snprintf(cred_plain, sizeof(cred_plain), "%s:%s",
                 client->config.username, client->config.password);

        char cred_b64[4 * ((2 * NTRIP_MAX_CRED_LEN + 2) / 3 + 2)];
        size_t b64_len = base64_encode(
            (const uint8_t*)cred_plain, strlen(cred_plain),
            cred_b64, sizeof(cred_b64));
        if (b64_len == 0) {
            client->last_error = NTRIP_ERR_INTERNAL;
            return NTRIP_ERR_INTERNAL;
        }
        rb_printf(&rb, "Authorization: Basic %s\r\n", cred_b64);
    }

    rb_append(&rb, "Connection: close\r\n");
    rb_append(&rb, "\r\n");

    if (rb.overflow) {
        client->last_error = NTRIP_ERR_REQUEST_TOO_LARGE;
        return NTRIP_ERR_REQUEST_TOO_LARGE;
    }

    client->request_len = rb.pos;
    client->request_sent_offset = 0;
    return NTRIP_OK;
}

/* ---- Parse HTTP status code from response buffer ---- */

static int ntrip_parse_http_status(const uint8_t* data, size_t len)
{
    /* Look for "HTTP/x.x NNN" pattern */
    if (len < 12) return 0;
    if (data[0] != 'H' || data[1] != 'T' || data[2] != 'T' || data[3] != 'P') return 0;

    const uint8_t* p = data + 4;
    /* Skip HTTP version and space */
    while (p < data + len && *p != ' ') p++;
    if (p >= data + len) return 0;
    p++; /* skip space */

    /* Parse 3-digit status code */
    int status = 0;
    for (int i = 0; i < 3 && p < data + len; i++, p++) {
        if (*p < '0' || *p > '9') return 0;
        status = status * 10 + (*p - '0');
    }
    return status;
}

/* ---- Clear RTCM buffer helper ---- */

static void ntrip_clear_rtcm(ntrip_client_t* client)
{
    client->rtcm_buffer.head = 0;
    client->rtcm_buffer.tail = 0;
    client->rtcm_buffer.size = 0;
    client->rtcm_buffer.overflow_count = 0;
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
    client->connect_attempted = false;
    client->config_valid = false;
    client->http_status_code = 0;
    byte_ring_buffer_init(&client->rtcm_buffer,
                          client->rtcm_storage,
                          sizeof(client->rtcm_storage));
    client->component.service_step = ntrip_client_service_step;
}

void ntrip_client_configure(ntrip_client_t* client, const ntrip_client_config_t* config)
{
    if (client == NULL) {
        return;
    }
    if (config == NULL) {
        client->config_valid = false;
        return;
    }
    memcpy(&client->config, config, sizeof(ntrip_client_config_t));
    /* Valid only when host AND mountpoint are non-empty */
    client->config_valid = (config->host[0] != '\0' && config->mountpoint[0] != '\0');
}

void ntrip_client_set_transport(ntrip_client_t* client, transport_tcp_t* transport)
{
    if (client == NULL) {
        return;
    }
    client->transport = transport;
}

bool ntrip_client_start(ntrip_client_t* client)
{
    if (client == NULL) {
        return false;
    }
    if (client->state != NTRIP_STATE_IDLE) {
        return false;
    }
    if (!client->config_valid) {
        return false;
    }
    if (client->transport == NULL) {
        return false;
    }
    client->started = true;
    client->last_error = NTRIP_OK;
    client->http_status_code = 0;
    return ntrip_client_transition(client, NTRIP_STATE_CONNECTING, 0);
}

bool ntrip_client_is_started(const ntrip_client_t* client)
{
    if (client == NULL) {
        return false;
    }
    return client->started;
}

/* Legacy compatibility — wraps raw buffer into transport reference.
 * This is a DEPRECATED bridge: creates a minimal transport_tcp_t on the
 * stack, wires rx_buffer, and stores a pointer.  Caller must keep the
 * transport alive.  Prefer ntrip_client_set_transport() for new code. */
void ntrip_client_set_tcp_source(ntrip_client_t* client, byte_ring_buffer_t* source)
{
    (void)client;
    (void)source;
    /* Deprecated: no-op.  Use ntrip_client_set_transport() instead. */
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

    if (new_state == NTRIP_STATE_RETRY_WAIT) {
        client->reconnect_count++;
    }

    if (new_state == NTRIP_STATE_CONNECTED) {
        client->reconnect_count = 0;
        client->last_error = NTRIP_OK;
        /* Note: http_status_code is intentionally NOT reset here
         * so the caller can inspect the HTTP status that led to CONNECTED. */
    }

    /* When entering retry cycle, reset request/response state */
    if (new_state == NTRIP_STATE_CONNECTING && client->reconnect_count > 0) {
        client->connect_attempted = false;
        client->request_sent_offset = 0;
        client->request_len = 0;
        client->response_received = 0;
        client->http_status_code = 0;
    }

    /* When entering ERROR, disconnect transport */
    if (new_state == NTRIP_STATE_ERROR) {
        if (client->transport != NULL) {
            transport_tcp_disconnect(client->transport);
        }
        ntrip_clear_rtcm(client);
    }

    /* When leaving connected state, clear RTCM buffer */
    if (new_state != NTRIP_STATE_CONNECTED) {
        ntrip_clear_rtcm(client);
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

ntrip_err_t ntrip_client_get_last_error(const ntrip_client_t* client)
{
    if (client == NULL) {
        return NTRIP_ERR_INVALID_PARAM;
    }
    return client->last_error;
}

int ntrip_client_get_http_status(const ntrip_client_t* client)
{
    if (client == NULL) {
        return 0;
    }
    return client->http_status_code;
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
        client->last_state_change_us = 1;
    }

    uint64_t elapsed = timestamp_us - client->last_state_change_us;

    /* Process state transitions — loop for instant state chaining */
    bool progressed = true;
    while (progressed) {
        progressed = false;

        switch (client->state) {
        case NTRIP_STATE_IDLE:
            break;

        case NTRIP_STATE_CONNECTING:
            if (client->transport == NULL) {
                client->last_error = NTRIP_ERR_NOT_CONNECTED;
                ntrip_client_transition(client, NTRIP_STATE_ERROR, timestamp_us);
                progressed = true;
                break;
            }
            /* Trigger TCP connect on first entry (not repeatedly) */
            if (!client->connect_attempted) {
                transport_tcp_connect(client->transport);
                client->connect_attempted = true;
            }
            if (transport_tcp_is_connected(client->transport)) {
                ntrip_client_transition(client, NTRIP_STATE_BUILD_REQUEST, timestamp_us);
                progressed = true;
            }
            /* If not connected yet, wait (real ESP32: lwip connecting) */
            break;

        case NTRIP_STATE_BUILD_REQUEST: {
            ntrip_err_t err = ntrip_build_request(client);
            if (err == NTRIP_OK) {
                ntrip_client_transition(client, NTRIP_STATE_SEND_REQUEST, timestamp_us);
                progressed = true;
            } else {
                /* last_error already set by ntrip_build_request */
                ntrip_client_transition(client, NTRIP_STATE_ERROR, timestamp_us);
                progressed = true;
            }
            break;
        }

        case NTRIP_STATE_SEND_REQUEST: {
            if (client->transport == NULL ||
                !transport_tcp_is_connected(client->transport)) {
                client->last_error = NTRIP_ERR_NOT_CONNECTED;
                ntrip_client_transition(client, NTRIP_STATE_ERROR, timestamp_us);
                progressed = true;
                break;
            }

            size_t remaining = client->request_len - client->request_sent_offset;
            size_t written = transport_tcp_tx_write(
                client->transport,
                client->request_buf + client->request_sent_offset,
                remaining);
            client->request_sent_offset += written;

            if (client->request_sent_offset >= client->request_len) {
                /* Entire request sent */
                ntrip_client_transition(client, NTRIP_STATE_WAIT_RESPONSE, timestamp_us);
                progressed = true;
            }
            /* If partial write, stay in SEND_REQUEST (loop exits, retry next cycle) */
            break;
        }

        case NTRIP_STATE_WAIT_RESPONSE: {
            /* Read available response data */
            if (client->transport != NULL) {
                size_t space = sizeof(client->response_buf) - client->response_received;
                if (space > 0) {
                    size_t n = transport_tcp_rx_read(
                        client->transport,
                        client->response_buf + client->response_received,
                        space);
                    client->response_received += n;
                }
            }

            /* Try to parse HTTP status line */
            int status = ntrip_parse_http_status(
                client->response_buf, client->response_received);

            if (status != 0) {
                client->http_status_code = status;

                if (status == 200) {
                    ntrip_client_transition(client, NTRIP_STATE_CONNECTED, timestamp_us);
                    progressed = true;
                } else if (status == 401) {
                    client->last_error = NTRIP_ERR_AUTH_FAILED;
                    ntrip_client_transition(client, NTRIP_STATE_ERROR, timestamp_us);
                    progressed = true;
                } else if (status == 403) {
                    client->last_error = NTRIP_ERR_FORBIDDEN;
                    ntrip_client_transition(client, NTRIP_STATE_ERROR, timestamp_us);
                    progressed = true;
                } else if (status == 404) {
                    client->last_error = NTRIP_ERR_NOT_FOUND;
                    ntrip_client_transition(client, NTRIP_STATE_ERROR, timestamp_us);
                    progressed = true;
                } else {
                    client->last_error = NTRIP_ERR_INTERNAL;
                    ntrip_client_transition(client, NTRIP_STATE_ERROR, timestamp_us);
                    progressed = true;
                }
                break;
            }

            /* Check for timeout */
            uint64_t timeout_us = (uint64_t)client->config.timeout_ms * 1000u;
            if (timeout_us > 0 && elapsed >= timeout_us) {
                client->last_error = NTRIP_ERR_TIMEOUT;
                ntrip_client_transition(client, NTRIP_STATE_ERROR, timestamp_us);
                progressed = true;
            }
            break;
        }

        case NTRIP_STATE_CONNECTED:
            /* Forward RTCM data from TCP transport to RTCM output buffer */
            if (client->transport == NULL) {
                break;
            }
            {
                uint8_t tmp[128];
                size_t available = transport_tcp_rx_available(client->transport);
                if (available > 0) {
                    size_t to_read = available > sizeof(tmp) ? sizeof(tmp) : available;
                    size_t pulled = transport_tcp_rx_read(client->transport, tmp, to_read);
                    if (pulled > 0) {
                        byte_ring_buffer_write(&client->rtcm_buffer, tmp, pulled);
                    }
                }
            }
            break;

        case NTRIP_STATE_ERROR:
            /* Auto-transition to RETRY_WAIT immediately */
            ntrip_client_transition(client, NTRIP_STATE_RETRY_WAIT, timestamp_us);
            progressed = true;
            break;

        case NTRIP_STATE_RETRY_WAIT: {
            uint64_t backoff_us = (uint64_t)client->config.reconnect_backoff_ms * 1000u;
            if (backoff_us > 0 && elapsed >= backoff_us) {
                ntrip_client_transition(client, NTRIP_STATE_CONNECTING, timestamp_us);
                progressed = true;
            }
            break;
        }
        }
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
