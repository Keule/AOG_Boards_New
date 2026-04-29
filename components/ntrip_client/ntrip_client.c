/**
 * NAV-NTRIP-001: Produktiver NTRIP-Client auf generischem TCP-Transport
 *
 * Implements the NTRIP protocol state machine:
 *   IDLE → CONNECTING → SEND_REQUEST → WAIT_RESPONSE → STREAMING
 *   (any connected state) → ERROR → RETRY_WAIT → CONNECTING
 *
 * HTTP request generation with Basic Auth (RFC 7617).
 * Response parsing: ICY 200 OK, HTTP/1.0 200, HTTP/1.1 200.
 * RTCM data forwarding in STREAMING state via transport_tcp RX.
 */

#include "ntrip_client.h"
#include <string.h>
#include <stdio.h>

/* ---- Internal error codes ---- */

#define NTRIP_ERR_NO_TRANSPORT      -1
#define NTRIP_ERR_NO_CONFIG         -2
#define NTRIP_ERR_NO_MOUNTPOINT     -3
#define NTRIP_ERR_REQUEST_TOO_LARGE -4
#define NTRIP_ERR_RESPONSE_TOO_LARGE -5
#define NTRIP_ERR_INVALID_RESPONSE  -6
#define NTRIP_ERR_DISCONNECTED      -7
#define NTRIP_ERR_TIMEOUT           -8

/* ---- Base64 encoder (RFC 4648) ---- */

static const char s_base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t base64_encode(const uint8_t* input, size_t input_len,
                            char* output, size_t output_size)
{
    size_t out_len = 0;
    size_t i;

    for (i = 0; i < input_len; i += 3) {
        size_t remaining = input_len - i;
        uint32_t n = (uint32_t)input[i] << 16;
        if (remaining > 1) n |= (uint32_t)input[i + 1] << 8;
        if (remaining > 2) n |= (uint32_t)input[i + 2];

        if (out_len + 4 > output_size) {
            break;
        }

        output[out_len++] = s_base64_table[(n >> 18) & 0x3F];
        output[out_len++] = s_base64_table[(n >> 12) & 0x3F];
        output[out_len++] = (remaining > 1) ? s_base64_table[(n >> 6) & 0x3F] : '=';
        output[out_len++] = (remaining > 2) ? s_base64_table[n & 0x3F] : '=';
    }

    if (out_len < output_size) {
        output[out_len] = '\0';
    }
    return out_len;
}

/* ---- String length helper (NULL-safe) ---- */

static size_t safe_strlen(const char* s)
{
    return (s != NULL) ? strlen(s) : 0;
}

/* ---- HTTP/NTRIP Request Builder ---- */

/**
 * Build NTRIP HTTP request string.
 * Returns total bytes written (excluding null terminator), or negative on error.
 */
static int build_ntrip_request(const ntrip_client_config_t* config,
                               uint8_t* buf, size_t buf_size)
{
    int pos = 0;
    const char* mountpoint = (config->mountpoint != NULL) ? config->mountpoint : "";
    const char* user_agent = (config->user_agent != NULL)
                              ? config->user_agent
                              : NTRIP_DEFAULT_USER_AGENT;

    /* Request line: GET /<mountpoint> HTTP/1.0 */
    pos += snprintf((char*)buf + pos, buf_size - (size_t)pos,
                    "GET /%s HTTP/1.0\r\n", mountpoint);

    /* User-Agent header */
    pos += snprintf((char*)buf + pos, buf_size - (size_t)pos,
                    "User-Agent: %s\r\n", user_agent);

    /* Authorization: Basic <base64> (only if username provided) */
    if (config->username != NULL && config->username[0] != '\0') {
        char credentials[128];
        char b64[96];
        int cred_len = snprintf(credentials, sizeof(credentials), "%s:%s",
                                config->username,
                                (config->password != NULL) ? config->password : "");
        if (cred_len > 0 && (size_t)cred_len < sizeof(credentials)) {
            base64_encode((const uint8_t*)credentials, (size_t)cred_len,
                          b64, sizeof(b64));
            pos += snprintf((char*)buf + pos, buf_size - (size_t)pos,
                            "Authorization: Basic %s\r\n", b64);
        }
    }

    /* Ntrip-Version header */
    pos += snprintf((char*)buf + pos, buf_size - (size_t)pos,
                    "Ntrip-Version: Ntrip/2.0\r\n");

    /* Connection: close */
    pos += snprintf((char*)buf + pos, buf_size - (size_t)pos,
                    "Connection: close\r\n");

    /* End of headers */
    pos += snprintf((char*)buf + pos, buf_size - (size_t)pos, "\r\n");

    return pos;
}

/* ---- Response Parser ---- */

/**
 * Parse accumulated HTTP response to extract status code and header end offset.
 * Looks for "\r\n\r\n" to find end of headers.
 * Returns true if headers are complete and status was parsed.
 */
static bool parse_ntrip_response(const uint8_t* data, size_t len,
                                 int* out_status_code, size_t* out_header_end)
{
    *out_status_code = -1;
    *out_header_end = 0;

    /* Find end of headers */
    size_t i;
    for (i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n') {

            *out_header_end = i + 4;

            /* Parse status line: "ICY NNN" or "HTTP/1.x NNN" */
            if (len >= 4 && memcmp(data, "ICY", 3) == 0) {
                /* ICY NNN ... */
                if (i >= 7 && data[3] == ' ') {
                    int code = 0;
                    size_t j;
                    for (j = 4; j < i && j < 7; j++) {
                        if (data[j] >= '0' && data[j] <= '9') {
                            code = code * 10 + (data[j] - '0');
                        } else {
                            break;
                        }
                    }
                    if (code > 0) {
                        *out_status_code = code;
                        return true;
                    }
                }
            } else if (len >= 12 && memcmp(data, "HTTP/1.", 7) == 0) {
                /* HTTP/1.x NNN ... */
                if (i >= 12 && data[8] == ' ') {
                    int code = 0;
                    size_t j;
                    for (j = 9; j < i && j < 12; j++) {
                        if (data[j] >= '0' && data[j] <= '9') {
                            code = code * 10 + (data[j] - '0');
                        } else {
                            break;
                        }
                    }
                    if (code > 0) {
                        *out_status_code = code;
                        return true;
                    }
                }
            }

            /* Headers found but status not recognized — still return true,
             * caller should treat unrecognized status as error */
            *out_status_code = 0;
            return true;
        }
    }

    return false;  /* Headers not complete yet */
}

/* ---- Clear RTCM buffer helper ---- */

static void clear_rtcm_buffer(ntrip_client_t* client)
{
    client->rtcm_buffer.head = 0;
    client->rtcm_buffer.tail = 0;
    client->rtcm_buffer.size = 0;
    client->rtcm_buffer.overflow_count = 0;
}

/* ---- State name ---- */

const char* ntrip_client_state_name(ntrip_state_t state)
{
    switch (state) {
    case NTRIP_STATE_IDLE:          return "idle";
    case NTRIP_STATE_CONNECTING:    return "connecting";
    case NTRIP_STATE_SEND_REQUEST:  return "send_request";
    case NTRIP_STATE_WAIT_RESPONSE: return "wait_response";
    case NTRIP_STATE_STREAMING:     return "streaming";
    case NTRIP_STATE_ERROR:         return "error";
    case NTRIP_STATE_RETRY_WAIT:    return "retry_wait";
    default:                        return "unknown";
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
        return to == NTRIP_STATE_SEND_REQUEST || to == NTRIP_STATE_ERROR;
    case NTRIP_STATE_SEND_REQUEST:
        return to == NTRIP_STATE_WAIT_RESPONSE || to == NTRIP_STATE_ERROR;
    case NTRIP_STATE_WAIT_RESPONSE:
        return to == NTRIP_STATE_STREAMING || to == NTRIP_STATE_ERROR;
    case NTRIP_STATE_STREAMING:
        return to == NTRIP_STATE_ERROR || to == NTRIP_STATE_IDLE;
    case NTRIP_STATE_ERROR:
        return to == NTRIP_STATE_RETRY_WAIT || to == NTRIP_STATE_IDLE;
    case NTRIP_STATE_RETRY_WAIT:
        return to == NTRIP_STATE_CONNECTING || to == NTRIP_STATE_IDLE;
    default:
        return false;
    }
}

/* ---- Public API: Lifecycle ---- */

void ntrip_client_init(ntrip_client_t* client)
{
    if (client == NULL) {
        return;
    }
    memset(client, 0, sizeof(ntrip_client_t));
    client->state = NTRIP_STATE_IDLE;
    client->started = false;
    client->last_error_code = 0;
    byte_ring_buffer_init(&client->rtcm_buffer,
                          client->rtcm_storage,
                          sizeof(client->rtcm_storage));
    client->component.service_step = ntrip_client_service_step;
}

hal_err_t ntrip_client_configure(ntrip_client_t* client, const ntrip_client_config_t* config)
{
    if (client == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    if (config == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    client->config = config;
    return HAL_OK;
}

void ntrip_client_set_transport(ntrip_client_t* client, transport_tcp_t* tcp)
{
    if (client == NULL) {
        return;
    }
    client->tcp = tcp;
}

void ntrip_client_config_set_defaults(ntrip_client_config_t* config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(ntrip_client_config_t));
    config->user_agent           = NTRIP_DEFAULT_USER_AGENT;
    config->reconnect_initial_ms = NTRIP_DEFAULT_INITIAL_BACKOFF_MS;
    config->reconnect_max_ms     = NTRIP_DEFAULT_MAX_BACKOFF_MS;
    config->response_timeout_ms  = NTRIP_DEFAULT_RESPONSE_TIMEOUT_MS;
}

bool ntrip_client_start(ntrip_client_t* client)
{
    if (client == NULL) {
        return false;
    }
    if (client->state != NTRIP_STATE_IDLE) {
        return false;
    }
    client->started = true;
    client->request_sent = false;
    client->request_len = 0;
    client->response_len = 0;
    client->last_error_code = 0;
    client->connect_attempted = false;
    client->current_backoff_ms = (client->config != NULL)
                                  ? client->config->reconnect_initial_ms
                                  : NTRIP_DEFAULT_INITIAL_BACKOFF_MS;
    return ntrip_client_transition(client, NTRIP_STATE_CONNECTING, 0);
}

bool ntrip_client_stop(ntrip_client_t* client)
{
    if (client == NULL) {
        return false;
    }
    if (client->state == NTRIP_STATE_IDLE) {
        return false;
    }
    client->started = false;
    /* Force IDLE from any state — stop bypasses normal transition rules.
     * Also disconnect TCP transport if connected. */
    if (client->tcp != NULL && transport_tcp_is_connected(client->tcp)) {
        transport_tcp_disconnect(client->tcp);
    }
    client->state = NTRIP_STATE_IDLE;
    client->last_state_change_us = 0;
    clear_rtcm_buffer(client);
    return true;
}

/* ---- Public API: State Machine ---- */

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

    if (new_state == NTRIP_STATE_STREAMING) {
        client->reconnect_count = 0;
        /* Reset backoff on successful connection */
        client->current_backoff_ms = (client->config != NULL)
                                      ? client->config->reconnect_initial_ms
                                      : NTRIP_DEFAULT_INITIAL_BACKOFF_MS;
    }

    /* Clear RTCM buffer when not in streaming state */
    if (new_state != NTRIP_STATE_STREAMING) {
        clear_rtcm_buffer(client);
    }

    /* Reset response buffer when entering new states that need fresh data */
    if (new_state == NTRIP_STATE_CONNECTING || new_state == NTRIP_STATE_SEND_REQUEST) {
        client->response_len = 0;
        client->request_sent = false;
        client->request_len = 0;
        client->connect_attempted = false;
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

bool ntrip_client_is_started(const ntrip_client_t* client)
{
    if (client == NULL) {
        return false;
    }
    return client->started;
}

uint32_t ntrip_client_get_reconnect_count(const ntrip_client_t* client)
{
    if (client == NULL) {
        return 0;
    }
    return client->reconnect_count;
}

int ntrip_client_get_last_error_code(const ntrip_client_t* client)
{
    if (client == NULL) {
        return 0;
    }
    return client->last_error_code;
}

/* ---- Internal: elapsed time helper ---- */

static uint64_t elapsed_us(ntrip_client_t* client, uint64_t timestamp_us)
{
    if (client->last_state_change_us == 0) {
        return timestamp_us;
    }
    return timestamp_us - client->last_state_change_us;
}

/* ---- Internal: transition to ERROR ---- */

static void go_error(ntrip_client_t* client, int error_code, uint64_t timestamp_us)
{
    client->last_error_code = error_code;
    ntrip_client_transition(client, NTRIP_STATE_ERROR, timestamp_us);
}

/* ---- Service Step ---- */

void ntrip_client_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    ntrip_client_t* client = (ntrip_client_t*)comp;
    if (client == NULL || !client->started) {
        return;
    }

    /* Check for transport disconnect in any active state */
    if (client->tcp != NULL &&
        (client->state == NTRIP_STATE_SEND_REQUEST ||
         client->state == NTRIP_STATE_WAIT_RESPONSE ||
         client->state == NTRIP_STATE_STREAMING)) {
        if (!transport_tcp_is_connected(client->tcp)) {
            go_error(client, NTRIP_ERR_DISCONNECTED, timestamp_us);
            return;
        }
    }

    uint32_t timeout_ms = (client->config != NULL)
                           ? client->config->response_timeout_ms
                           : NTRIP_DEFAULT_RESPONSE_TIMEOUT_MS;
    uint64_t timeout_us = (uint64_t)timeout_ms * 1000u;

    switch (client->state) {
    case NTRIP_STATE_IDLE:
        break;

    case NTRIP_STATE_CONNECTING: {
        /* Initiate TCP connection */
        if (!client->connect_attempted) {
            if (client->tcp == NULL) {
                go_error(client, NTRIP_ERR_NO_TRANSPORT, timestamp_us);
                break;
            }
            transport_tcp_connect(client->tcp);
            client->connect_attempted = true;
        }

        /* Check if connected */
        if (client->tcp != NULL && transport_tcp_is_connected(client->tcp)) {
            ntrip_client_transition(client, NTRIP_STATE_SEND_REQUEST, timestamp_us);
            break;
        }

        /* Timeout */
        if (elapsed_us(client, timestamp_us) >= timeout_us) {
            go_error(client, NTRIP_ERR_TIMEOUT, timestamp_us);
        }
        break;
    }

    case NTRIP_STATE_SEND_REQUEST: {
        if (client->tcp == NULL || client->config == NULL) {
            go_error(client, NTRIP_ERR_NO_TRANSPORT, timestamp_us);
            break;
        }

        /* Build HTTP request on first entry */
        if (!client->request_sent && client->request_len == 0) {
            if (client->config->mountpoint == NULL) {
                go_error(client, NTRIP_ERR_NO_MOUNTPOINT, timestamp_us);
                break;
            }

            int req_len = build_ntrip_request(client->config,
                                              client->request_buf,
                                              sizeof(client->request_buf));
            if (req_len < 0 || (size_t)req_len >= sizeof(client->request_buf)) {
                go_error(client, NTRIP_ERR_REQUEST_TOO_LARGE, timestamp_us);
                break;
            }
            client->request_len = (size_t)req_len;
        }

        /* Write request to transport TCP TX buffer */
        if (client->request_len > 0 && !client->request_sent) {
            size_t written = transport_tcp_tx_write(client->tcp,
                                                     client->request_buf,
                                                     client->request_len);
            if (written >= client->request_len) {
                /* Full request written */
                client->request_sent = true;
                ntrip_client_transition(client, NTRIP_STATE_WAIT_RESPONSE, timestamp_us);
            }
            /* If partial write, retry on next service_step */
        }

        /* Timeout */
        if (elapsed_us(client, timestamp_us) >= timeout_us) {
            go_error(client, NTRIP_ERR_TIMEOUT, timestamp_us);
        }
        break;
    }

    case NTRIP_STATE_WAIT_RESPONSE: {
        if (client->tcp == NULL) {
            go_error(client, NTRIP_ERR_NO_TRANSPORT, timestamp_us);
            break;
        }

        /* Read data from transport TCP RX buffer into response buffer */
        size_t rx_avail = transport_tcp_rx_available(client->tcp);
        if (rx_avail > 0) {
            size_t space = sizeof(client->response_buf) - client->response_len;
            if (space == 0) {
                /* Response too large */
                go_error(client, NTRIP_ERR_RESPONSE_TOO_LARGE, timestamp_us);
                break;
            }
            size_t to_read = (rx_avail < space) ? rx_avail : space;
            size_t actual = transport_tcp_rx_read(client->tcp,
                                                   client->response_buf + client->response_len,
                                                   to_read);
            client->response_len += actual;
        }

        /* Try to parse response */
        int status_code = -1;
        size_t header_end = 0;
        bool complete = parse_ntrip_response(client->response_buf,
                                              client->response_len,
                                              &status_code,
                                              &header_end);

        if (complete) {
            if (status_code == 200) {
                /* Success — forward any data after headers to RTCM buffer */
                if (header_end < client->response_len) {
                    size_t rtcm_bytes = client->response_len - header_end;
                    byte_ring_buffer_write(&client->rtcm_buffer,
                                           client->response_buf + header_end,
                                           rtcm_bytes);
                }
                ntrip_client_transition(client, NTRIP_STATE_STREAMING, timestamp_us);
            } else if (status_code > 0) {
                /* HTTP error (401, 403, 404, etc.) */
                go_error(client, status_code, timestamp_us);
            } else {
                /* Unrecognized status */
                go_error(client, NTRIP_ERR_INVALID_RESPONSE, timestamp_us);
            }
            break;
        }

        /* Timeout */
        if (elapsed_us(client, timestamp_us) >= timeout_us) {
            go_error(client, NTRIP_ERR_TIMEOUT, timestamp_us);
        }
        break;
    }

    case NTRIP_STATE_STREAMING: {
        if (client->tcp == NULL) {
            go_error(client, NTRIP_ERR_NO_TRANSPORT, timestamp_us);
            break;
        }

        /* Forward RTCM data from transport TCP RX to RTCM buffer */
        uint8_t tmp[128];
        size_t available = transport_tcp_rx_available(client->tcp);
        while (available > 0) {
            size_t to_read = (available > sizeof(tmp)) ? sizeof(tmp) : available;
            size_t pulled = transport_tcp_rx_read(client->tcp, tmp, to_read);
            if (pulled == 0) break;

            byte_ring_buffer_write(&client->rtcm_buffer, tmp, pulled);
            available = transport_tcp_rx_available(client->tcp);
        }
        break;
    }

    case NTRIP_STATE_ERROR: {
        /* Auto-reconnect with backoff */
        uint32_t backoff_us = (client->current_backoff_ms != 0)
                               ? (uint64_t)client->current_backoff_ms * 1000u
                               : 1000000u;
        if (elapsed_us(client, timestamp_us) >= backoff_us) {
            ntrip_client_transition(client, NTRIP_STATE_RETRY_WAIT, timestamp_us);
            /* Immediately advance to CONNECTING */
            ntrip_client_transition(client, NTRIP_STATE_CONNECTING, timestamp_us);
        }
        break;
    }

    case NTRIP_STATE_RETRY_WAIT: {
        /* Backoff timer — transition to CONNECTING when expired */
        uint32_t backoff_us = (client->current_backoff_ms != 0)
                               ? (uint64_t)client->current_backoff_ms * 1000u
                               : 1000000u;
        if (elapsed_us(client, timestamp_us) >= backoff_us) {
            /* Double backoff for next retry (exponential) */
            uint32_t max_backoff = (client->config != NULL)
                                    ? client->config->reconnect_max_ms
                                    : NTRIP_DEFAULT_MAX_BACKOFF_MS;
            if (client->current_backoff_ms < max_backoff) {
                uint32_t next = client->current_backoff_ms * 2u;
                client->current_backoff_ms = (next < max_backoff) ? next : max_backoff;
            }
            ntrip_client_transition(client, NTRIP_STATE_CONNECTING, timestamp_us);
        }
        break;
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
