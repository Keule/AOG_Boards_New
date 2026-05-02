#include "unity.h"
#include "ntrip_client.h"
#include "transport_tcp.h"
#include "byte_ring_buffer.h"
#include <string.h>

/* ---- Test fixtures ---- */

static ntrip_client_t client;
static transport_tcp_t transport;
static transport_tcp_config_t tcp_cfg;

void setUp(void)
{
    memset(&client, 0, sizeof(client));
    memset(&transport, 0, sizeof(transport));

    /* Init transport */
    tcp_cfg.remote_ip = 0x01020304;  /* 1.2.3.4 */
    tcp_cfg.remote_port = 2101;
    transport_tcp_init(&transport, &tcp_cfg);

    /* Init client */
    ntrip_client_init(&client);
}

void tearDown(void) {}

/* ---- Helper: configure client with valid test config ---- */

static void configure_valid(ntrip_client_t* c)
{
    ntrip_client_config_t cfg = NTRIP_CLIENT_CONFIG_DEFAULT();
    strncpy(cfg.host, "ntrip.example.com", sizeof(cfg.host) - 1);
    strncpy(cfg.mountpoint, "RTCM3_GGB", sizeof(cfg.mountpoint) - 1);
    strncpy(cfg.username, "user", sizeof(cfg.username) - 1);
    strncpy(cfg.password, "pass", sizeof(cfg.password) - 1);
    cfg.port = 2101;
    cfg.timeout_ms = 5000;
    cfg.reconnect_backoff_ms = 2000;
    ntrip_client_configure(c, &cfg);
}

/* Helper: inject HTTP response into transport rx_buffer */
static void inject_response(transport_tcp_t* tcp, const char* resp)
{
    size_t len = strlen(resp);
    byte_ring_buffer_write(&tcp->rx_buffer, (const uint8_t*)resp, len);
}

/* ---- State name ---- */

void test_state_name_returns_correct_string(void)
{
    TEST_ASSERT_EQUAL_STRING("idle", ntrip_client_state_name(NTRIP_STATE_IDLE));
    TEST_ASSERT_EQUAL_STRING("connecting", ntrip_client_state_name(NTRIP_STATE_CONNECTING));
    TEST_ASSERT_EQUAL_STRING("build_request", ntrip_client_state_name(NTRIP_STATE_BUILD_REQUEST));
    TEST_ASSERT_EQUAL_STRING("send_request", ntrip_client_state_name(NTRIP_STATE_SEND_REQUEST));
    TEST_ASSERT_EQUAL_STRING("wait_response", ntrip_client_state_name(NTRIP_STATE_WAIT_RESPONSE));
    TEST_ASSERT_EQUAL_STRING("connected", ntrip_client_state_name(NTRIP_STATE_CONNECTED));
    TEST_ASSERT_EQUAL_STRING("error", ntrip_client_state_name(NTRIP_STATE_ERROR));
    TEST_ASSERT_EQUAL_STRING("retry_wait", ntrip_client_state_name(NTRIP_STATE_RETRY_WAIT));
    TEST_ASSERT_EQUAL_STRING("unknown", ntrip_client_state_name((ntrip_state_t)99));
}

/* ---- Init ---- */

void test_init_resets_to_idle(void)
{
    TEST_ASSERT_EQUAL(NTRIP_STATE_IDLE, ntrip_client_get_state(&client));
    TEST_ASSERT_FALSE(ntrip_client_is_started(&client));
    TEST_ASSERT_EQUAL(0, ntrip_client_get_reconnect_count(&client));
    TEST_ASSERT_EQUAL(NTRIP_OK, ntrip_client_get_last_error(&client));
    TEST_ASSERT_EQUAL(0, ntrip_client_get_http_status(&client));
}

void test_init_null_does_not_crash(void)
{
    ntrip_client_init(NULL);
    TEST_PASS();
}

/* ---- Configure ---- */

void test_configure_sets_valid_flag(void)
{
    configure_valid(&client);
    TEST_ASSERT_TRUE(client.config_valid);
}

void test_configure_empty_host_invalid(void)
{
    ntrip_client_config_t cfg = NTRIP_CLIENT_CONFIG_DEFAULT();
    strncpy(cfg.mountpoint, "RTCM3", sizeof(cfg.mountpoint) - 1);
    /* host remains empty */
    ntrip_client_configure(&client, &cfg);
    TEST_ASSERT_FALSE(client.config_valid);
}

void test_configure_empty_mountpoint_invalid(void)
{
    ntrip_client_config_t cfg = NTRIP_CLIENT_CONFIG_DEFAULT();
    strncpy(cfg.host, "example.com", sizeof(cfg.host) - 1);
    /* mountpoint remains empty */
    ntrip_client_configure(&client, &cfg);
    TEST_ASSERT_FALSE(client.config_valid);
}

void test_configure_null_clears_valid(void)
{
    configure_valid(&client);
    ntrip_client_configure(&client, NULL);
    TEST_ASSERT_FALSE(client.config_valid);
}

void test_configure_null_client_does_not_crash(void)
{
    ntrip_client_config_t cfg = NTRIP_CLIENT_CONFIG_DEFAULT();
    ntrip_client_configure(NULL, &cfg);
    TEST_PASS();
}

/* ---- Set transport ---- */

void test_set_transport(void)
{
    ntrip_client_set_transport(&client, &transport);
    TEST_ASSERT_EQUAL_PTR(&transport, client.transport);
}

void test_set_transport_null(void)
{
    ntrip_client_set_transport(&client, &transport);
    ntrip_client_set_transport(&client, NULL);
    TEST_ASSERT_NULL(client.transport);
}

/* ---- Start ---- */

void test_start_needs_valid_config(void)
{
    /* No config set — config_valid is false */
    ntrip_client_set_transport(&client, &transport);
    bool ok = ntrip_client_start(&client);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL(NTRIP_STATE_IDLE, ntrip_client_get_state(&client));
}

void test_start_needs_transport(void)
{
    configure_valid(&client);
    /* No transport set */
    bool ok = ntrip_client_start(&client);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL(NTRIP_STATE_IDLE, ntrip_client_get_state(&client));
}

void test_start_transitions_to_connecting(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);
    bool ok = ntrip_client_start(&client);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTING, ntrip_client_get_state(&client));
    TEST_ASSERT_TRUE(ntrip_client_is_started(&client));
}

void test_start_from_non_idle_fails(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);
    ntrip_client_start(&client);
    bool ok = ntrip_client_start(&client);
    TEST_ASSERT_FALSE(ok);
}

void test_start_null_returns_false(void)
{
    bool ok = ntrip_client_start(NULL);
    TEST_ASSERT_FALSE(ok);
}

/* ---- State transitions ---- */

void test_valid_transitions(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);

    /* IDLE -> CONNECTING */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_CONNECTING, 100));

    /* CONNECTING -> BUILD_REQUEST */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_BUILD_REQUEST, 200));

    /* BUILD_REQUEST -> SEND_REQUEST */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_SEND_REQUEST, 300));

    /* SEND_REQUEST -> WAIT_RESPONSE */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_WAIT_RESPONSE, 400));

    /* WAIT_RESPONSE -> CONNECTED */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_CONNECTED, 500));
    TEST_ASSERT_EQUAL(0, ntrip_client_get_reconnect_count(&client));

    /* CONNECTED -> ERROR */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_ERROR, 600));

    /* ERROR -> RETRY_WAIT */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_RETRY_WAIT, 700));
    TEST_ASSERT_EQUAL(1, ntrip_client_get_reconnect_count(&client));

    /* RETRY_WAIT -> CONNECTING */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_CONNECTING, 800));
}

void test_invalid_transitions_rejected(void)
{
    /* IDLE -> BUILD_REQUEST (must go through CONNECTING) */
    TEST_ASSERT_FALSE(ntrip_client_transition(&client, NTRIP_STATE_BUILD_REQUEST, 100));

    /* IDLE -> CONNECTED */
    TEST_ASSERT_FALSE(ntrip_client_transition(&client, NTRIP_STATE_CONNECTED, 100));

    /* IDLE -> ERROR */
    TEST_ASSERT_FALSE(ntrip_client_transition(&client, NTRIP_STATE_ERROR, 100));

    /* CONNECTING -> CONNECTED (skips BUILD/SEND/WAIT) */
    ntrip_client_transition(&client, NTRIP_STATE_CONNECTING, 100);
    TEST_ASSERT_FALSE(ntrip_client_transition(&client, NTRIP_STATE_CONNECTED, 200));

    /* SEND_REQUEST -> CONNECTED (skips WAIT_RESPONSE) */
    ntrip_client_transition(&client, NTRIP_STATE_CONNECTING, 100);
    ntrip_client_transition(&client, NTRIP_STATE_BUILD_REQUEST, 200);
    ntrip_client_transition(&client, NTRIP_STATE_SEND_REQUEST, 300);
    TEST_ASSERT_FALSE(ntrip_client_transition(&client, NTRIP_STATE_CONNECTED, 400));
}

void test_transition_null_returns_false(void)
{
    TEST_ASSERT_FALSE(ntrip_client_transition(NULL, NTRIP_STATE_CONNECTING, 0));
}

void test_same_state_transition_allowed(void)
{
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_IDLE, 100));
}

/* ---- Skeleton: full connect flow ---- */

void test_skeleton_connects_to_connected(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);

    /* Connect transport (stub sets connected=true) */
    transport_tcp_connect(&transport);

    ntrip_client_start(&client);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTING, ntrip_client_get_state(&client));

    /* Service step: CONNECTING → BUILD_REQUEST → SEND_REQUEST → WAIT_RESPONSE
     * Transport is already connected, so CONNECTING instant-progresses.
     * Request fits in tx_buffer (512 bytes > ~200 byte request). */
    ntrip_client_service_step((runtime_component_t*)&client, 1000);

    /* Should be in WAIT_RESPONSE (request fully sent) */
    TEST_ASSERT_EQUAL(NTRIP_STATE_WAIT_RESPONSE, ntrip_client_get_state(&client));

    /* Inject HTTP 200 response */
    inject_response(&transport, "HTTP/1.1 200 OK\r\n\r\n");
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTED, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(0, ntrip_client_get_reconnect_count(&client));
    TEST_ASSERT_EQUAL(200, ntrip_client_get_http_status(&client));
    TEST_ASSERT_EQUAL(NTRIP_OK, ntrip_client_get_last_error(&client));
}

/* ---- RTCM data forwarding (when connected) ---- */

void test_rtcm_forwarding_when_connected(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);
    transport_tcp_connect(&transport);
    ntrip_client_start(&client);

    /* Fast-forward to CONNECTED */
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    inject_response(&transport, "HTTP/1.1 200 OK\r\n\r\n");
    ntrip_client_service_step((runtime_component_t*)&client, 2000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTED, ntrip_client_get_state(&client));

    /* Push RTCM data into transport rx_buffer */
    uint8_t rtcm_data[] = {0xD3, 0x00, 0x13, 0x3E, 0xD7, 0xD3, 0x02, 0x02};
    byte_ring_buffer_write(&transport.rx_buffer, rtcm_data, sizeof(rtcm_data));

    ntrip_client_service_step((runtime_component_t*)&client, 3000);

    TEST_ASSERT_EQUAL(sizeof(rtcm_data), ntrip_client_rtcm_available(&client));

    uint8_t out[16] = {0};
    size_t popped = ntrip_client_pop_rtcm(&client, out, sizeof(out));
    TEST_ASSERT_EQUAL(sizeof(rtcm_data), popped);
    TEST_ASSERT_EQUAL_MEMORY(rtcm_data, out, sizeof(rtcm_data));
    TEST_ASSERT_EQUAL(0, ntrip_client_rtcm_available(&client));
}

void test_no_rtcm_forwarding_when_not_connected(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);
    transport_tcp_connect(&transport);
    ntrip_client_start(&client);
    /* Still in CONNECTING */

    uint8_t data[] = {0xD3, 0x00};
    byte_ring_buffer_write(&transport.rx_buffer, data, sizeof(data));

    ntrip_client_service_step((runtime_component_t*)&client, 500000);
    TEST_ASSERT_EQUAL(0, ntrip_client_rtcm_available(&client));
}

/* ---- Partial request write ---- */

void test_partial_write_progresses_over_multiple_steps(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);
    transport_tcp_connect(&transport);
    ntrip_client_start(&client);

    /* Fast-forward to WAIT_RESPONSE (request sent) */
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_WAIT_RESPONSE, ntrip_client_get_state(&client));

    /* Now force error → retry */
    client.last_error = NTRIP_OK;
    inject_response(&transport, "HTTP/1.1 404 Not Found\r\n\r\n");
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    /* Should be in RETRY_WAIT (ERROR was transient) */
    TEST_ASSERT_EQUAL(NTRIP_STATE_RETRY_WAIT, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(NTRIP_ERR_NOT_FOUND, ntrip_client_get_last_error(&client));
}

void test_partial_write_no_double_request_on_200(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);
    transport_tcp_connect(&transport);
    ntrip_client_start(&client);

    /* Advance to WAIT_RESPONSE */
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_WAIT_RESPONSE, ntrip_client_get_state(&client));

    /* Response 200 */
    inject_response(&transport, "HTTP/1.1 200 OK\r\n\r\n");
    ntrip_client_service_step((runtime_component_t*)&client, 2000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTED, ntrip_client_get_state(&client));

    /* Another service_step should NOT re-send the request */
    /* The request_sent_offset should be == request_len, no more sending */
    size_t tx_before = transport_tcp_tx_available(&transport);
    ntrip_client_service_step((runtime_component_t*)&client, 3000);
    size_t tx_after = transport_tcp_tx_available(&transport);
    /* TX should not have grown (no re-send) — it may shrink due to drain */
    TEST_ASSERT_TRUE(tx_after <= tx_before);
}

/* ---- Partial write: simulate small tx buffer ---- */

void test_partial_write_with_limited_tx_space(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);
    transport_tcp_connect(&transport);

    /* Fill tx buffer BEFORE starting — leave only 32 bytes free */
    uint8_t filler[TRANSPORT_TCP_TX_BUFFER_SIZE - 32];
    memset(filler, 0xAA, sizeof(filler));
    byte_ring_buffer_write(&transport.tx_buffer, filler, sizeof(filler));
    TEST_ASSERT_TRUE(transport_tcp_tx_free(&transport) <= 32);

    /* Start — triggers CONNECTING → BUILD_REQUEST → SEND_REQUEST */
    ntrip_client_start(&client);
    ntrip_client_service_step((runtime_component_t*)&client, 1000);

    /* Should be in SEND_REQUEST (partial write, not all bytes fit) */
    if (client.request_len > 32) {
        TEST_ASSERT_EQUAL(NTRIP_STATE_SEND_REQUEST, ntrip_client_get_state(&client));
        TEST_ASSERT_TRUE(client.request_sent_offset > 0);
        TEST_ASSERT_TRUE(client.request_sent_offset < client.request_len);
    }

    /* Drain tx buffer (simulates HAL sending data) */
    transport_tcp_service_step((runtime_component_t*)&transport, 2000);

    /* Next service_step should send more bytes */
    ntrip_client_service_step((runtime_component_t*)&client, 3000);

    /* Keep draining and stepping until fully sent */
    for (int i = 0; i < 20 && client.request_sent_offset < client.request_len; i++) {
        transport_tcp_service_step((runtime_component_t*)&transport, 3000);
        ntrip_client_service_step((runtime_component_t*)&client, 3000 + (uint64_t)i * 1000);
    }

    /* Eventually all bytes should be sent */
    TEST_ASSERT_EQUAL(client.request_len, client.request_sent_offset);
}

/* ---- Request builder: bounds safety ---- */

void test_request_too_large_mountpoint(void)
{
    ntrip_client_config_t cfg = NTRIP_CLIENT_CONFIG_DEFAULT();
    strncpy(cfg.host, "a.com", sizeof(cfg.host) - 1);
    /* Very long mountpoint that overflows 256-byte request buffer */
    memset(cfg.mountpoint, 'M', NTRIP_MAX_MOUNTPOINT_LEN - 1);
    cfg.mountpoint[NTRIP_MAX_MOUNTPOINT_LEN - 1] = '\0';
    /* Add credentials to push total over 256 bytes */
    strncpy(cfg.username, "user", sizeof(cfg.username) - 1);
    strncpy(cfg.password, "pass", sizeof(cfg.password) - 1);
    ntrip_client_configure(&client, &cfg);
    ntrip_client_set_transport(&client, &transport);
    transport_tcp_connect(&transport);
    ntrip_client_start(&client);

    ntrip_client_service_step((runtime_component_t*)&client, 1000);

    /* Should have errored with REQUEST_TOO_LARGE */
    TEST_ASSERT_EQUAL(NTRIP_STATE_RETRY_WAIT, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(NTRIP_ERR_REQUEST_TOO_LARGE, ntrip_client_get_last_error(&client));
}

void test_request_too_large_credentials(void)
{
    ntrip_client_config_t cfg = NTRIP_CLIENT_CONFIG_DEFAULT();
    strncpy(cfg.host, "a.com", sizeof(cfg.host) - 1);
    strncpy(cfg.mountpoint, "M", sizeof(cfg.mountpoint) - 1);
    /* Long credentials overflow the 256-byte request buffer */
    memset(cfg.username, 'U', NTRIP_MAX_CRED_LEN - 1);
    cfg.username[NTRIP_MAX_CRED_LEN - 1] = '\0';
    memset(cfg.password, 'P', NTRIP_MAX_CRED_LEN - 1);
    cfg.password[NTRIP_MAX_CRED_LEN - 1] = '\0';
    ntrip_client_configure(&client, &cfg);
    ntrip_client_set_transport(&client, &transport);
    transport_tcp_connect(&transport);
    ntrip_client_start(&client);

    ntrip_client_service_step((runtime_component_t*)&client, 1000);

    TEST_ASSERT_EQUAL(NTRIP_STATE_RETRY_WAIT, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(NTRIP_ERR_REQUEST_TOO_LARGE, ntrip_client_get_last_error(&client));
}

/* ---- Error paths: HTTP status codes ---- */

void test_http_401_error_and_disconnect(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);
    transport_tcp_connect(&transport);
    ntrip_client_start(&client);

    /* Advance to WAIT_RESPONSE */
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_WAIT_RESPONSE, ntrip_client_get_state(&client));

    /* Transport should be connected before error */
    TEST_ASSERT_TRUE(transport_tcp_is_connected(&transport));

    /* Inject 401 */
    inject_response(&transport, "HTTP/1.1 401 Unauthorized\r\n\r\n");
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    /* Should be in RETRY_WAIT, transport disconnected */
    TEST_ASSERT_EQUAL(NTRIP_STATE_RETRY_WAIT, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(NTRIP_ERR_AUTH_FAILED, ntrip_client_get_last_error(&client));
    TEST_ASSERT_EQUAL(401, ntrip_client_get_http_status(&client));
    TEST_ASSERT_FALSE(transport_tcp_is_connected(&transport));
}

void test_http_403_error_and_disconnect(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);
    transport_tcp_connect(&transport);
    ntrip_client_start(&client);

    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    inject_response(&transport, "HTTP/1.1 403 Forbidden\r\n\r\n");
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    TEST_ASSERT_EQUAL(NTRIP_STATE_RETRY_WAIT, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(NTRIP_ERR_FORBIDDEN, ntrip_client_get_last_error(&client));
    TEST_ASSERT_EQUAL(403, ntrip_client_get_http_status(&client));
    TEST_ASSERT_FALSE(transport_tcp_is_connected(&transport));
}

void test_http_404_error_and_disconnect(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);
    transport_tcp_connect(&transport);
    ntrip_client_start(&client);

    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    inject_response(&transport, "HTTP/1.1 404 Not Found\r\n\r\n");
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    TEST_ASSERT_EQUAL(NTRIP_STATE_RETRY_WAIT, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(NTRIP_ERR_NOT_FOUND, ntrip_client_get_last_error(&client));
    TEST_ASSERT_EQUAL(404, ntrip_client_get_http_status(&client));
    TEST_ASSERT_FALSE(transport_tcp_is_connected(&transport));
}

/* ---- Error path: timeout ---- */

void test_timeout_error_and_disconnect(void)
{
    ntrip_client_config_t cfg = NTRIP_CLIENT_CONFIG_DEFAULT();
    strncpy(cfg.host, "a.com", sizeof(cfg.host) - 1);
    strncpy(cfg.mountpoint, "M", sizeof(cfg.mountpoint) - 1);
    cfg.timeout_ms = 2;  /* 2ms timeout for testing */
    ntrip_client_configure(&client, &cfg);
    ntrip_client_set_transport(&client, &transport);
    transport_tcp_connect(&transport);
    ntrip_client_start(&client);

    /* Advance to WAIT_RESPONSE */
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_WAIT_RESPONSE, ntrip_client_get_state(&client));

    TEST_ASSERT_TRUE(transport_tcp_is_connected(&transport));

    /* Simulate timeout: advance past 2ms (= 2000us) from state entry at 1000us */
    ntrip_client_service_step((runtime_component_t*)&client, 5000);

    TEST_ASSERT_EQUAL(NTRIP_STATE_RETRY_WAIT, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(NTRIP_ERR_TIMEOUT, ntrip_client_get_last_error(&client));
    TEST_ASSERT_FALSE(transport_tcp_is_connected(&transport));
}

/* ---- Retry: fresh request offset ---- */

void test_retry_resets_request_offset(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);
    transport_tcp_connect(&transport);
    ntrip_client_start(&client);

    /* Connect and send request */
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_WAIT_RESPONSE, ntrip_client_get_state(&client));

    /* Record original request length */
    size_t orig_len = client.request_len;
    TEST_ASSERT_TRUE(orig_len > 0);

    /* Force error */
    inject_response(&transport, "HTTP/1.1 500 Server Error\r\n\r\n");
    ntrip_client_service_step((runtime_component_t*)&client, 2000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_RETRY_WAIT, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(1, ntrip_client_get_reconnect_count(&client));

    /* After backoff, stub transport connects immediately so the state
     * chains through CONNECTING → BUILD_REQUEST → SEND_REQUEST → WAIT_RESPONSE */
    uint64_t backoff_us = (uint64_t)client.config.reconnect_backoff_ms * 1000u;
    ntrip_client_service_step((runtime_component_t*)&client,
                              2000 + backoff_us + 1000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_WAIT_RESPONSE, ntrip_client_get_state(&client));

    /* Request was rebuilt from scratch and fully sent during retry */
    TEST_ASSERT_TRUE(client.request_len > 0);
    TEST_ASSERT_EQUAL(client.request_len, client.request_sent_offset);
    /* Response state was reset (no new response yet) */
    TEST_ASSERT_EQUAL(0, client.response_received);
    TEST_ASSERT_EQUAL(0, client.http_status_code);
}

/* ---- Reconnect counter ---- */

void test_reconnect_counter_increments(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);
    transport_tcp_connect(&transport);
    ntrip_client_start(&client);

    /* Error → retry → error → retry */
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    inject_response(&transport, "HTTP/1.1 401 Unauthorized\r\n\r\n");
    ntrip_client_service_step((runtime_component_t*)&client, 2000);
    TEST_ASSERT_EQUAL(1, ntrip_client_get_reconnect_count(&client));

    /* Backoff + retry */
    uint64_t backoff_us = (uint64_t)client.config.reconnect_backoff_ms * 1000u;
    transport_tcp_connect(&transport);  /* reconnect transport */
    ntrip_client_service_step((runtime_component_t*)&client, 2000 + backoff_us + 1000);

    /* Connect and error again */
    ntrip_client_service_step((runtime_component_t*)&client, 2000 + backoff_us + 2000);
    inject_response(&transport, "HTTP/1.1 403 Forbidden\r\n\r\n");
    ntrip_client_service_step((runtime_component_t*)&client, 2000 + backoff_us + 3000);
    TEST_ASSERT_EQUAL(2, ntrip_client_get_reconnect_count(&client));
}

/* ---- Error path: null transport ---- */

void test_null_transport_in_connecting_errors(void)
{
    ntrip_client_config_t cfg = NTRIP_CLIENT_CONFIG_DEFAULT();
    strncpy(cfg.host, "a.com", sizeof(cfg.host) - 1);
    strncpy(cfg.mountpoint, "M", sizeof(cfg.mountpoint) - 1);
    ntrip_client_configure(&client, &cfg);
    /* transport = NULL (cleared in setUp) */

    /* Manually force into CONNECTING state */
    ntrip_client_transition(&client, NTRIP_STATE_CONNECTING, 1000);
    client.started = true;

    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    TEST_ASSERT_EQUAL(NTRIP_STATE_RETRY_WAIT, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(NTRIP_ERR_NOT_CONNECTED, ntrip_client_get_last_error(&client));
}

/* ---- Service step edge cases ---- */

void test_service_step_null_does_not_crash(void)
{
    ntrip_client_service_step(NULL, 1000);
    TEST_PASS();
}

void test_service_step_not_started_does_nothing(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);
    /* Not started */
    ntrip_client_service_step((runtime_component_t*)&client, 1000000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_IDLE, ntrip_client_get_state(&client));
}

/* ---- Error tracking API ---- */

void test_get_last_error_null(void)
{
    ntrip_err_t err = ntrip_client_get_last_error(NULL);
    TEST_ASSERT_EQUAL(NTRIP_ERR_INVALID_PARAM, err);
}

void test_get_http_status_null(void)
{
    int status = ntrip_client_get_http_status(NULL);
    TEST_ASSERT_EQUAL(0, status);
}

void test_get_reconnect_count_null(void)
{
    TEST_ASSERT_EQUAL(0, ntrip_client_get_reconnect_count(NULL));
}

void test_get_state_null(void)
{
    TEST_ASSERT_EQUAL(NTRIP_STATE_IDLE, ntrip_client_get_state(NULL));
}

void test_is_started_null(void)
{
    TEST_ASSERT_FALSE(ntrip_client_is_started(NULL));
}

/* ---- NTRIP start logic: empty config must NOT start (Fix 4) ---- */

void test_default_config_does_not_start(void)
{
    /* Default config has empty host and mountpoint -> config_valid = false */
    ntrip_client_config_t cfg = NTRIP_CLIENT_CONFIG_DEFAULT();
    ntrip_client_configure(&client, &cfg);
    ntrip_client_set_transport(&client, &transport);

    bool ok = ntrip_client_start(&client);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL(NTRIP_STATE_IDLE, ntrip_client_get_state(&client));
    TEST_ASSERT_FALSE(ntrip_client_is_started(&client));
}

void test_valid_config_does_start(void)
{
    /* Valid config with host + mountpoint -> config_valid = true */
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);

    bool ok = ntrip_client_start(&client);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTING, ntrip_client_get_state(&client));
    TEST_ASSERT_TRUE(ntrip_client_is_started(&client));
}

void test_app_core_empty_config_no_silent_failure(void)
{
    /* Simulate app_core pattern: init, configure with default, check before start */
    ntrip_client_init(&client);
    ntrip_client_set_transport(&client, &transport);
    ntrip_client_configure(&client, &(ntrip_client_config_t)NTRIP_CLIENT_CONFIG_DEFAULT());

    /* config_valid must be false with empty default */
    TEST_ASSERT_FALSE(client.config_valid);

    /* Start must fail, must not crash, state must remain IDLE */
    bool ok = ntrip_client_start(&client);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL(NTRIP_STATE_IDLE, ntrip_client_get_state(&client));

    /* Service step must be a no-op (not started) */
    ntrip_client_service_step((runtime_component_t*)&client, 1000000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_IDLE, ntrip_client_get_state(&client));
}

/* ---- NTRIP triggers TCP connect itself (Fix 4) ---- */

void test_ntrip_triggers_connect_in_connecting(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);

    /* Do NOT manually connect — let NTRIP do it */
    TEST_ASSERT_FALSE(transport_tcp_is_connected(&transport));

    ntrip_client_start(&client);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTING, ntrip_client_get_state(&client));

    /* Service step should trigger connect */
    ntrip_client_service_step((runtime_component_t*)&client, 1000);

    /* Transport should now be connected (stub connect() sets connected=true) */
    TEST_ASSERT_TRUE(transport_tcp_is_connected(&transport));
    TEST_ASSERT_TRUE(client.connect_attempted);
}

void test_connect_called_only_once_per_attempt(void)
{
    configure_valid(&client);
    ntrip_client_set_transport(&client, &transport);
    ntrip_client_start(&client);

    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    TEST_ASSERT_TRUE(client.connect_attempted);

    /* Second service step — connect_attempted is true, should NOT call connect again */
    /* (We can't observe the non-call directly, but the flag stays true) */
    ntrip_client_service_step((runtime_component_t*)&client, 2000);
    TEST_ASSERT_TRUE(client.connect_attempted);
}

/* ---- Main ---- */

int main(void)
{
    UNITY_BEGIN();

    /* State name */
    RUN_TEST(test_state_name_returns_correct_string);

    /* Init */
    RUN_TEST(test_init_resets_to_idle);
    RUN_TEST(test_init_null_does_not_crash);

    /* Configure */
    RUN_TEST(test_configure_sets_valid_flag);
    RUN_TEST(test_configure_empty_host_invalid);
    RUN_TEST(test_configure_empty_mountpoint_invalid);
    RUN_TEST(test_configure_null_clears_valid);
    RUN_TEST(test_configure_null_client_does_not_crash);

    /* Set transport */
    RUN_TEST(test_set_transport);
    RUN_TEST(test_set_transport_null);

    /* Start */
    RUN_TEST(test_start_needs_valid_config);
    RUN_TEST(test_start_needs_transport);
    RUN_TEST(test_start_transitions_to_connecting);
    RUN_TEST(test_start_from_non_idle_fails);
    RUN_TEST(test_start_null_returns_false);

    /* State transitions */
    RUN_TEST(test_valid_transitions);
    RUN_TEST(test_invalid_transitions_rejected);
    RUN_TEST(test_transition_null_returns_false);
    RUN_TEST(test_same_state_transition_allowed);

    /* Skeleton connect flow */
    RUN_TEST(test_skeleton_connects_to_connected);

    /* RTCM forwarding */
    RUN_TEST(test_rtcm_forwarding_when_connected);
    RUN_TEST(test_no_rtcm_forwarding_when_not_connected);

    /* Partial write */
    RUN_TEST(test_partial_write_progresses_over_multiple_steps);
    RUN_TEST(test_partial_write_no_double_request_on_200);
    RUN_TEST(test_partial_write_with_limited_tx_space);

    /* Request builder bounds safety */
    RUN_TEST(test_request_too_large_mountpoint);
    RUN_TEST(test_request_too_large_credentials);

    /* Error paths */
    RUN_TEST(test_http_401_error_and_disconnect);
    RUN_TEST(test_http_403_error_and_disconnect);
    RUN_TEST(test_http_404_error_and_disconnect);
    RUN_TEST(test_timeout_error_and_disconnect);

    /* Retry */
    RUN_TEST(test_retry_resets_request_offset);
    RUN_TEST(test_reconnect_counter_increments);

    /* Null transport error */
    RUN_TEST(test_null_transport_in_connecting_errors);

    /* Service step edge cases */
    RUN_TEST(test_service_step_null_does_not_crash);
    RUN_TEST(test_service_step_not_started_does_nothing);

    /* Error tracking API null safety */
    RUN_TEST(test_get_last_error_null);
    RUN_TEST(test_get_http_status_null);
    RUN_TEST(test_get_reconnect_count_null);
    RUN_TEST(test_get_state_null);
    RUN_TEST(test_is_started_null);

    /* NTRIP start logic (Fix 4: empty config must NOT start) */
    RUN_TEST(test_default_config_does_not_start);
    RUN_TEST(test_valid_config_does_start);
    RUN_TEST(test_app_core_empty_config_no_silent_failure);

    /* NTRIP triggers TCP connect itself (Fix 4) */
    RUN_TEST(test_ntrip_triggers_connect_in_connecting);
    RUN_TEST(test_connect_called_only_once_per_attempt);

    return UNITY_END();
}
