/**
 * NAV-NTRIP-001: NTRIP Client Host Tests
 *
 * Tests cover:
 *   - Request generation (format, Basic Auth)
 *   - State transitions
 *   - ICY 200 OK / HTTP 200 → STREAMING
 *   - HTTP error responses (401, 403, 404) → ERROR
 *   - Timeout handling
 *   - RTCM data forwarding
 *   - Reconnect / exponential backoff
 *   - Disconnect handling
 *   - No RTCM output in non-streaming states
 */

#include "unity.h"
#include "ntrip_client.h"
#include "transport_tcp.h"
#include "byte_ring_buffer.h"
#include "hal_backend.h"
#include <stdio.h>
#include <string.h>

/* ==========================================================================
 * Fake TCP HAL — controllable connect/recv/send behavior
 * ========================================================================== */

static uint8_t  s_fake_rx_data[4096];
static size_t  s_fake_rx_len  = 0;
static size_t  s_fake_rx_pos  = 0;
static uint8_t s_fake_tx_written[4096];
static size_t  s_fake_tx_written_len = 0;
static bool    s_fake_connected = false;
static bool    s_fake_connect_fails = false;

static hal_err_t fake_connect(uint32_t ip, uint16_t port)
{
    (void)ip; (void)port;
    if (s_fake_connect_fails) return HAL_ERR_IO;
    s_fake_connected = true;
    return HAL_OK;
}

static hal_err_t fake_disconnect(void)
{
    s_fake_connected = false;
    return HAL_OK;
}

static int fake_recv(uint8_t* buf, size_t max_len)
{
    if (!s_fake_connected) return 0;
    size_t avail = s_fake_rx_len - s_fake_rx_pos;
    if (avail == 0) return 0;
    size_t n = avail > max_len ? max_len : avail;
    memcpy(buf, s_fake_rx_data + s_fake_rx_pos, n);
    s_fake_rx_pos += n;
    return (int)n;
}

static int fake_send(const uint8_t* buf, size_t len)
{
    if (!s_fake_connected) return -1;
    memcpy(s_fake_tx_written + s_fake_tx_written_len, buf, len);
    s_fake_tx_written_len += len;
    return (int)len;
}

static bool fake_is_connected(void) { return s_fake_connected; }

static const transport_tcp_hal_ops_t s_fake_hal = {
    .connect = fake_connect, .disconnect = fake_disconnect,
    .recv = fake_recv, .send = fake_send, .is_connected = fake_is_connected,
};

static void reset_fake(void)
{
    memset(s_fake_rx_data, 0, sizeof(s_fake_rx_data));
    s_fake_rx_len = 0;
    s_fake_rx_pos = 0;
    memset(s_fake_tx_written, 0, sizeof(s_fake_tx_written));
    s_fake_tx_written_len = 0;
    s_fake_connected = false;
    s_fake_connect_fails = false;
}

static void feed_fake_rx(const uint8_t* data, size_t len)
{
    memcpy(s_fake_rx_data + s_fake_rx_len, data, len);
    s_fake_rx_len += len;
}

static void feed_fake_rx_str(const char* str)
{
    feed_fake_rx((const uint8_t*)str, strlen(str));
}

/* ==========================================================================
 * Test fixtures
 * ========================================================================== */

static ntrip_client_t client;
static transport_tcp_t tcp;
static ntrip_client_config_t config;

static void setup_client_with_config(void)
{
    reset_fake();
    memset(&client, 0, sizeof(client));
    memset(&tcp, 0, sizeof(tcp));
    memset(&config, 0, sizeof(config));

    ntrip_client_config_set_defaults(&config);
    config.mountpoint = "TEST_MOUNT";

    /* Init transport */
    transport_tcp_config_t tcp_cfg = { .remote_ip = 0x01020304, .remote_port = 2101 };
    transport_tcp_init(&tcp, &tcp_cfg);
    transport_tcp_set_hal_ops(&tcp, &s_fake_hal);

    /* Init and configure ntrip client */
    ntrip_client_init(&client);
    ntrip_client_configure(&client, &config);
    ntrip_client_set_transport(&client, &tcp);
}

void setUp(void)
{
    setup_client_with_config();
}

void tearDown(void) {}

/* ==========================================================================
 * Helper: fast-forward to STREAMING state
 * ========================================================================== */

static void fast_forward_to_streaming(const char* response)
{
    ntrip_client_start(&client);

    /* CONNECTING → SEND_REQUEST (transport is connected immediately) */
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_SEND_REQUEST, ntrip_client_get_state(&client));

    /* SEND_REQUEST → WAIT_RESPONSE (request written to TX buffer) */
    ntrip_client_service_step((runtime_component_t*)&client, 2000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_WAIT_RESPONSE, ntrip_client_get_state(&client));

    /* Feed response into fake TCP */
    feed_fake_rx_str(response);

    /* Let transport TCP service_step deliver data to its rx_buffer first,
     * then ntrip client service_step reads from there */
    transport_tcp_service_step(&tcp.component, 3000);
    ntrip_client_service_step((runtime_component_t*)&client, 3000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_STREAMING, ntrip_client_get_state(&client));
}

/* ==========================================================================
 * Helper: flush TX data via transport_tcp_service_step
 * ========================================================================== */

static void flush_tcp_tx(void)
{
    /* Call transport service_step to drain TX buffer through fake HAL send */
    transport_tcp_service_step(&tcp.component, 2000);
}

/* ==========================================================================
 * Teil 5: Request Generation Tests
 * ========================================================================== */

void test_request_contains_correct_mountpoint(void)
{
    ntrip_client_start(&client);
    ntrip_client_service_step((runtime_component_t*)&client, 1000); /* CONNECTING */
    ntrip_client_service_step((runtime_component_t*)&client, 2000); /* SEND_REQUEST */

    /* Flush TX so data goes through fake HAL send */
    flush_tcp_tx();

    TEST_ASSERT_TRUE(s_fake_tx_written_len > 0);
    char* get_line = strstr((char*)s_fake_tx_written, "GET /TEST_MOUNT");
    TEST_ASSERT_NOT_NULL(get_line);
}

void test_request_contains_http_10(void)
{
    ntrip_client_start(&client);
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    flush_tcp_tx();

    char* http = strstr((char*)s_fake_tx_written, "HTTP/1.0");
    TEST_ASSERT_NOT_NULL(http);
}

void test_request_contains_user_agent(void)
{
    ntrip_client_start(&client);
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    flush_tcp_tx();

    char* ua = strstr((char*)s_fake_tx_written, "User-Agent: AOG-ESP-Multiboard/1.0");
    TEST_ASSERT_NOT_NULL(ua);
}

void test_request_contains_ntrip_version(void)
{
    ntrip_client_start(&client);
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    flush_tcp_tx();

    char* nv = strstr((char*)s_fake_tx_written, "Ntrip-Version: Ntrip/2.0");
    TEST_ASSERT_NOT_NULL(nv);
}

void test_request_contains_connection_close(void)
{
    ntrip_client_start(&client);
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    flush_tcp_tx();

    char* cc = strstr((char*)s_fake_tx_written, "Connection: close");
    TEST_ASSERT_NOT_NULL(cc);
}

/* ==========================================================================
 * Teil 5: Basic Auth Tests
 * ========================================================================== */

void test_basic_auth_in_request_when_credentials_set(void)
{
    setup_client_with_config();
    config.username = "testuser";
    config.password = "testpass";

    ntrip_client_start(&client);
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    flush_tcp_tx();

    char* auth = strstr((char*)s_fake_tx_written, "Authorization: Basic ");
    TEST_ASSERT_NOT_NULL(auth);

    /* Verify Base64 encoding of "testuser:testpass" = "dGVzdHVzZXI6dGVzdHBhc3M=" */
    char* b64_start = auth + strlen("Authorization: Basic ");
    TEST_ASSERT_EQUAL(0, strncmp(b64_start, "dGVzdHVzZXI6dGVzdHBhc3M=\r\n", 26));
}

void test_basic_auth_correct_encoding_empty_password(void)
{
    setup_client_with_config();
    config.username = "user";
    config.password = "";

    ntrip_client_start(&client);
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    flush_tcp_tx();

    char* auth = strstr((char*)s_fake_tx_written, "Authorization: Basic ");
    TEST_ASSERT_NOT_NULL(auth);

    /* "user:" = "dXNlcjo=" */
    char* b64_start = auth + strlen("Authorization: Basic ");
    TEST_ASSERT_EQUAL(0, strncmp(b64_start, "dXNlcjo=\r\n", 10));
}

void test_no_basic_auth_when_no_username(void)
{
    /* Default config has NULL username */
    ntrip_client_start(&client);
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    flush_tcp_tx();

    char* auth = strstr((char*)s_fake_tx_written, "Authorization: Basic ");
    TEST_ASSERT_NULL(auth);
}

void test_custom_user_agent(void)
{
    setup_client_with_config();
    config.user_agent = "MyNTRIPClient/2.5";

    ntrip_client_start(&client);
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    flush_tcp_tx();

    char* ua = strstr((char*)s_fake_tx_written, "User-Agent: MyNTRIPClient/2.5");
    TEST_ASSERT_NOT_NULL(ua);
}

/* ==========================================================================
 * Teil 5: State Transition Tests
 * ========================================================================== */

void test_state_names_all_states(void)
{
    TEST_ASSERT_EQUAL_STRING("idle", ntrip_client_state_name(NTRIP_STATE_IDLE));
    TEST_ASSERT_EQUAL_STRING("connecting", ntrip_client_state_name(NTRIP_STATE_CONNECTING));
    TEST_ASSERT_EQUAL_STRING("send_request", ntrip_client_state_name(NTRIP_STATE_SEND_REQUEST));
    TEST_ASSERT_EQUAL_STRING("wait_response", ntrip_client_state_name(NTRIP_STATE_WAIT_RESPONSE));
    TEST_ASSERT_EQUAL_STRING("streaming", ntrip_client_state_name(NTRIP_STATE_STREAMING));
    TEST_ASSERT_EQUAL_STRING("error", ntrip_client_state_name(NTRIP_STATE_ERROR));
    TEST_ASSERT_EQUAL_STRING("retry_wait", ntrip_client_state_name(NTRIP_STATE_RETRY_WAIT));
    TEST_ASSERT_EQUAL_STRING("unknown", ntrip_client_state_name((ntrip_state_t)99));
}

void test_init_resets_to_idle(void)
{
    TEST_ASSERT_EQUAL(NTRIP_STATE_IDLE, ntrip_client_get_state(&client));
    TEST_ASSERT_FALSE(ntrip_client_is_started(&client));
    TEST_ASSERT_EQUAL(0, ntrip_client_get_reconnect_count(&client));
    TEST_ASSERT_EQUAL(0, ntrip_client_get_last_error_code(&client));
}

void test_start_from_idle_to_connecting(void)
{
    bool ok = ntrip_client_start(&client);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTING, ntrip_client_get_state(&client));
    TEST_ASSERT_TRUE(ntrip_client_is_started(&client));
}

void test_start_from_non_idle_fails(void)
{
    ntrip_client_start(&client);
    bool ok = ntrip_client_start(&client);
    TEST_ASSERT_FALSE(ok);
}

void test_stop_returns_to_idle(void)
{
    ntrip_client_start(&client);
    TEST_ASSERT_TRUE(ntrip_client_stop(&client));
    TEST_ASSERT_EQUAL(NTRIP_STATE_IDLE, ntrip_client_get_state(&client));
    TEST_ASSERT_FALSE(ntrip_client_is_started(&client));
}

void test_stop_from_idle_fails(void)
{
    bool ok = ntrip_client_stop(&client);
    TEST_ASSERT_FALSE(ok);
}

void test_stop_from_streaming_works(void)
{
    fast_forward_to_streaming("ICY 200 OK\r\n\r\n");
    TEST_ASSERT_TRUE(ntrip_client_stop(&client));
    TEST_ASSERT_EQUAL(NTRIP_STATE_IDLE, ntrip_client_get_state(&client));
}

void test_valid_transitions(void)
{
    /* IDLE → CONNECTING */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_CONNECTING, 100));
    /* CONNECTING → SEND_REQUEST */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_SEND_REQUEST, 200));
    /* SEND_REQUEST → WAIT_RESPONSE */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_WAIT_RESPONSE, 300));
    /* WAIT_RESPONSE → STREAMING */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_STREAMING, 400));
    /* STREAMING → ERROR */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_ERROR, 500));
    /* ERROR → RETRY_WAIT */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_RETRY_WAIT, 600));
    TEST_ASSERT_EQUAL(1, ntrip_client_get_reconnect_count(&client));
    /* RETRY_WAIT → CONNECTING */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_CONNECTING, 700));
}

void test_invalid_transitions_rejected(void)
{
    /* IDLE → STREAMING (skips states) */
    TEST_ASSERT_FALSE(ntrip_client_transition(&client, NTRIP_STATE_STREAMING, 100));
    /* IDLE → ERROR (skips states) */
    TEST_ASSERT_FALSE(ntrip_client_transition(&client, NTRIP_STATE_ERROR, 100));
    /* IDLE → WAIT_RESPONSE (skips states) */
    TEST_ASSERT_FALSE(ntrip_client_transition(&client, NTRIP_STATE_WAIT_RESPONSE, 100));

    /* From CONNECTING */
    ntrip_client_transition(&client, NTRIP_STATE_CONNECTING, 100);
    /* CONNECTING → STREAMING (skips SEND_REQUEST, WAIT_RESPONSE) */
    TEST_ASSERT_FALSE(ntrip_client_transition(&client, NTRIP_STATE_STREAMING, 200));
}

/* ==========================================================================
 * Teil 5: ICY 200 OK → STREAMING
 * ========================================================================== */

void test_icy_200_ok_transitions_to_streaming(void)
{
    fast_forward_to_streaming("ICY 200 OK\r\n\r\n");
}

void test_http_10_200_transitions_to_streaming(void)
{
    fast_forward_to_streaming("HTTP/1.0 200 OK\r\n\r\n");
}

void test_http_11_200_transitions_to_streaming(void)
{
    fast_forward_to_streaming("HTTP/1.1 200 OK\r\n\r\n");
}

void test_streaming_resets_reconnect_count(void)
{
    /* Manually set reconnect count */
    ntrip_client_transition(&client, NTRIP_STATE_CONNECTING, 100);
    ntrip_client_transition(&client, NTRIP_STATE_ERROR, 200);
    ntrip_client_transition(&client, NTRIP_STATE_RETRY_WAIT, 300);
    TEST_ASSERT_EQUAL(1, ntrip_client_get_reconnect_count(&client));

    /* Reset and fast-forward to streaming */
    setup_client_with_config();
    fast_forward_to_streaming("ICY 200 OK\r\n\r\n");
    TEST_ASSERT_EQUAL(0, ntrip_client_get_reconnect_count(&client));
}

/* ==========================================================================
 * Teil 5: HTTP Error Responses (401/404 → ERROR/RETRY)
 * ========================================================================== */

void test_http_401_goes_to_error(void)
{
    ntrip_client_start(&client);
    ntrip_client_service_step((runtime_component_t*)&client, 1000); /* → SEND_REQUEST */
    ntrip_client_service_step((runtime_component_t*)&client, 2000); /* → WAIT_RESPONSE */

    feed_fake_rx_str("HTTP/1.0 401 Unauthorized\r\n\r\n");
    transport_tcp_service_step(&tcp.component, 3000);
    ntrip_client_service_step((runtime_component_t*)&client, 3000);

    TEST_ASSERT_EQUAL(NTRIP_STATE_ERROR, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(401, ntrip_client_get_last_error_code(&client));
}

void test_http_403_goes_to_error(void)
{
    ntrip_client_start(&client);
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    feed_fake_rx_str("HTTP/1.0 403 Forbidden\r\n\r\n");
    transport_tcp_service_step(&tcp.component, 3000);
    ntrip_client_service_step((runtime_component_t*)&client, 3000);

    TEST_ASSERT_EQUAL(NTRIP_STATE_ERROR, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(403, ntrip_client_get_last_error_code(&client));
}

void test_http_404_goes_to_error(void)
{
    ntrip_client_start(&client);
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    feed_fake_rx_str("HTTP/1.0 404 Not Found\r\n\r\n");
    transport_tcp_service_step(&tcp.component, 3000);
    ntrip_client_service_step((runtime_component_t*)&client, 3000);

    TEST_ASSERT_EQUAL(NTRIP_STATE_ERROR, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(404, ntrip_client_get_last_error_code(&client));
}

void test_error_then_retry(void)
{
    ntrip_client_start(&client);
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    feed_fake_rx_str("HTTP/1.0 401 Unauthorized\r\n\r\n");
    transport_tcp_service_step(&tcp.component, 3000);
    ntrip_client_service_step((runtime_component_t*)&client, 3000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_ERROR, ntrip_client_get_state(&client));

    /* Wait for backoff (default 1s = 1000000us) */
    /* last_state_change_us was set to 3000 when entering ERROR state */
    ntrip_client_service_step((runtime_component_t*)&client, 2000000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTING, ntrip_client_get_state(&client));
    /* reconnect_count incremented when transitioning to RETRY_WAIT */
    TEST_ASSERT_EQUAL(1, ntrip_client_get_reconnect_count(&client));
}

/* ==========================================================================
 * Teil 5: RTCM Data Forwarding
 * ========================================================================== */

void test_rtcm_bytes_landed_in_rtcm_buffer(void)
{
    fast_forward_to_streaming("ICY 200 OK\r\n\r\n");

    /* Feed RTCM data through fake TCP */
    static const uint8_t rtcm_data[] = {0xD3, 0x00, 0x13, 0x3E, 0xD7, 0xD3, 0x02, 0x02};
    feed_fake_rx(rtcm_data, sizeof(rtcm_data));

    /* Let transport TCP deliver to rx_buffer, then ntrip forwards to rtcm_buffer */
    transport_tcp_service_step(&tcp.component, 4000);
    ntrip_client_service_step((runtime_component_t*)&client, 4000);

    TEST_ASSERT_EQUAL(sizeof(rtcm_data), ntrip_client_rtcm_available(&client));

    /* Pop and verify */
    uint8_t out[16] = {0};
    size_t popped = ntrip_client_pop_rtcm(&client, out, sizeof(out));
    TEST_ASSERT_EQUAL(sizeof(rtcm_data), popped);
    TEST_ASSERT_EQUAL_MEMORY(rtcm_data, out, sizeof(rtcm_data));
}

void test_rtcm_data_after_headers_in_response(void)
{
    /* Response with RTCM data immediately after headers */
    static const uint8_t rtcm_data[] = {0xD3, 0x00, 0x13, 0x3E};
    char combined[128];
    int hdr_len = snprintf(combined, sizeof(combined), "ICY 200 OK\r\n\r\n");
    memcpy(combined + hdr_len, rtcm_data, sizeof(rtcm_data));

    ntrip_client_start(&client);
    ntrip_client_service_step((runtime_component_t*)&client, 1000); /* → SEND_REQUEST */
    ntrip_client_service_step((runtime_component_t*)&client, 2000); /* → WAIT_RESPONSE */

    feed_fake_rx((const uint8_t*)combined, (size_t)hdr_len + sizeof(rtcm_data));
    transport_tcp_service_step(&tcp.component, 3000);
    ntrip_client_service_step((runtime_component_t*)&client, 3000);

    TEST_ASSERT_EQUAL(NTRIP_STATE_STREAMING, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(sizeof(rtcm_data), ntrip_client_rtcm_available(&client));
}

void test_no_rtcm_when_not_streaming(void)
{
    ntrip_client_start(&client);
    /* Still in CONNECTING state */

    static const uint8_t data[] = {0xD3, 0x00, 0x13};
    feed_fake_rx(data, sizeof(data));
    transport_tcp_service_step(&tcp.component, 500000);
    ntrip_client_service_step((runtime_component_t*)&client, 500000);

    TEST_ASSERT_EQUAL(0, ntrip_client_rtcm_available(&client));
}

/* ==========================================================================
 * Teil 5: Reconnect / Backoff
 * ========================================================================== */

void test_reconnect_after_error(void)
{
    ntrip_client_start(&client);
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    ntrip_client_service_step((runtime_component_t*)&client, 2000);

    /* Simulate error response */
    feed_fake_rx_str("HTTP/1.0 404 Not Found\r\n\r\n");
    transport_tcp_service_step(&tcp.component, 3000);
    ntrip_client_service_step((runtime_component_t*)&client, 3000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_ERROR, ntrip_client_get_state(&client));

    /* Wait for backoff (default 1 second) */
    /* ERROR entered at t=3000, backoff = 1000000us */
    ntrip_client_service_step((runtime_component_t*)&client, 2000000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTING, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(1, ntrip_client_get_reconnect_count(&client));
}

void test_exponential_backoff_doubles(void)
{
    setup_client_with_config();
    config.reconnect_initial_ms = 100;
    config.reconnect_max_ms = 500;

    ntrip_client_start(&client);

    /* First error → backoff 100ms → reconnect */
    ntrip_client_service_step((runtime_component_t*)&client, 1000);  /* → SEND_REQUEST */
    ntrip_client_service_step((runtime_component_t*)&client, 2000);  /* → WAIT_RESPONSE */
    feed_fake_rx_str("HTTP/1.0 404\r\n\r\n");
    transport_tcp_service_step(&tcp.component, 3000);
    ntrip_client_service_step((runtime_component_t*)&client, 3000);  /* → ERROR, last_change=3000 */
    TEST_ASSERT_EQUAL(NTRIP_STATE_ERROR, ntrip_client_get_state(&client));

    /* After 100ms (100000us) backoff → ERROR handler → RETRY_WAIT → CONNECTING */
    ntrip_client_service_step((runtime_component_t*)&client, 103000); /* 100ms later */
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTING, ntrip_client_get_state(&client));

    /* Error again → backoff should now be 200ms */
    ntrip_client_service_step((runtime_component_t*)&client, 104000); /* → SEND_REQUEST */
    ntrip_client_service_step((runtime_component_t*)&client, 105000); /* → WAIT_RESPONSE */
    feed_fake_rx_str("HTTP/1.0 404\r\n\r\n");
    transport_tcp_service_step(&tcp.component, 106000);
    ntrip_client_service_step((runtime_component_t*)&client, 106000); /* → ERROR, last_change=106000 */
    TEST_ASSERT_EQUAL(NTRIP_STATE_ERROR, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(1, ntrip_client_get_reconnect_count(&client));

    /* After 200ms (200000us) backoff → RETRY_WAIT → CONNECTING */
    ntrip_client_service_step((runtime_component_t*)&client, 306000); /* 200ms later */
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTING, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(2, ntrip_client_get_reconnect_count(&client));
}

void test_backoff_capped_at_max(void)
{
    setup_client_with_config();
    config.reconnect_initial_ms = 100;
    config.reconnect_max_ms = 200;

    ntrip_client_start(&client);

    /* First error */
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    ntrip_client_service_step((runtime_component_t*)&client, 2000);
    feed_fake_rx_str("HTTP/1.0 404\r\n\r\n");
    transport_tcp_service_step(&tcp.component, 3000);
    ntrip_client_service_step((runtime_component_t*)&client, 3000); /* ERROR */

    /* Retry (backoff=100ms) */
    ntrip_client_service_step((runtime_component_t*)&client, 103000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTING, ntrip_client_get_state(&client));

    /* Second error (backoff doubled to 200ms) */
    ntrip_client_service_step((runtime_component_t*)&client, 104000);
    ntrip_client_service_step((runtime_component_t*)&client, 105000);
    feed_fake_rx_str("HTTP/1.0 404\r\n\r\n");
    transport_tcp_service_step(&tcp.component, 106000);
    ntrip_client_service_step((runtime_component_t*)&client, 106000); /* ERROR, backoff=200 */

    /* Third error: backoff would be 400ms but capped at 200ms */
    ntrip_client_service_step((runtime_component_t*)&client, 306000); /* → CONNECTING */
    ntrip_client_service_step((runtime_component_t*)&client, 307000);
    ntrip_client_service_step((runtime_component_t*)&client, 308000);
    feed_fake_rx_str("HTTP/1.0 404\r\n\r\n");
    transport_tcp_service_step(&tcp.component, 309000);
    ntrip_client_service_step((runtime_component_t*)&client, 309000); /* ERROR */
    TEST_ASSERT_EQUAL(NTRIP_STATE_ERROR, ntrip_client_get_state(&client));

    /* Verify backoff is still 200ms (capped) */
    ntrip_client_service_step((runtime_component_t*)&client, 509000); /* 200ms later */
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTING, ntrip_client_get_state(&client));
}

/* ==========================================================================
 * Timeout Tests
 * ========================================================================== */

void test_connect_timeout_goes_to_error(void)
{
    /* Set short timeout AFTER setup (setup resets config to defaults) */
    setup_client_with_config();
    config.response_timeout_ms = 1000;

    /* Make connect succeed but never send response */
    s_fake_connected = true;

    ntrip_client_start(&client);
    /* CONNECTING → SEND_REQUEST immediately */
    ntrip_client_service_step((runtime_component_t*)&client, 1000);
    ntrip_client_service_step((runtime_component_t*)&client, 2000); /* SEND_REQUEST → WAIT_RESPONSE */
    ntrip_client_service_step((runtime_component_t*)&client, 3000); /* WAIT_RESPONSE (no timeout yet) */

    /* Wait for timeout (1000ms = 1000000us after entering WAIT_RESPONSE at t=2000) */
    ntrip_client_service_step((runtime_component_t*)&client, 3000000); /* 1s later */
    TEST_ASSERT_EQUAL(NTRIP_STATE_ERROR, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(-8, ntrip_client_get_last_error_code(&client)); /* NTRIP_ERR_TIMEOUT */
}

/* ==========================================================================
 * Error Cases
 * ========================================================================== */

void test_no_transport_goes_to_error(void)
{
    ntrip_client_t bare_client;
    memset(&bare_client, 0, sizeof(bare_client));
    ntrip_client_config_t bare_config;
    ntrip_client_config_set_defaults(&bare_config);
    bare_config.mountpoint = "MOUNT";
    ntrip_client_init(&bare_client);
    ntrip_client_configure(&bare_client, &bare_config);
    /* No transport set! */

    ntrip_client_start(&bare_client);
    ntrip_client_service_step((runtime_component_t*)&bare_client, 1000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_ERROR, ntrip_client_get_state(&bare_client));
    TEST_ASSERT_EQUAL(-1, ntrip_client_get_last_error_code(&bare_client));
}

void test_no_config_goes_to_error(void)
{
    ntrip_client_t bare_client;
    transport_tcp_t bare_tcp;
    memset(&bare_client, 0, sizeof(bare_client));
    memset(&bare_tcp, 0, sizeof(bare_tcp));
    ntrip_client_init(&bare_client);
    /* No config set! */

    transport_tcp_config_t tcp_cfg = { .remote_ip = 0, .remote_port = 2101 };
    transport_tcp_init(&bare_tcp, &tcp_cfg);
    transport_tcp_set_hal_ops(&bare_tcp, &s_fake_hal);
    ntrip_client_set_transport(&bare_client, &bare_tcp);

    ntrip_client_start(&bare_client);
    /* Will reach SEND_REQUEST and fail there because config is NULL */
    ntrip_client_service_step((runtime_component_t*)&bare_client, 1000);
    ntrip_client_service_step((runtime_component_t*)&bare_client, 2000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_ERROR, ntrip_client_get_state(&bare_client));
}

void test_disconnect_during_streaming_goes_to_error(void)
{
    fast_forward_to_streaming("ICY 200 OK\r\n\r\n");

    /* Simulate disconnect: set fake_connected=false, then run transport
     * service_step to propagate to tcp.connected, then ntrip service_step */
    s_fake_connected = false;
    transport_tcp_service_step(&tcp.component, 5000000);
    ntrip_client_service_step((runtime_component_t*)&client, 5000000);

    TEST_ASSERT_EQUAL(NTRIP_STATE_ERROR, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(-7, ntrip_client_get_last_error_code(&client)); /* NTRIP_ERR_DISCONNECTED */
}

/* ==========================================================================
 * Edge Cases / Null Safety
 * ========================================================================== */

void test_init_null_does_not_crash(void)
{
    ntrip_client_init(NULL);
    TEST_PASS();
}

void test_start_null_returns_false(void)
{
    bool ok = ntrip_client_start(NULL);
    TEST_ASSERT_FALSE(ok);
}

void test_service_step_null_does_not_crash(void)
{
    ntrip_client_service_step(NULL, 1000);
    TEST_PASS();
}

void test_service_step_not_started_does_nothing(void)
{
    ntrip_client_service_step((runtime_component_t*)&client, 1000000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_IDLE, ntrip_client_get_state(&client));
}

void test_configure_null_returns_error(void)
{
    hal_err_t err = ntrip_client_configure(&client, NULL);
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, err);
}

void test_pop_rtcm_null_safety(void)
{
    uint8_t buf[8];
    TEST_ASSERT_EQUAL(0, ntrip_client_pop_rtcm(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(0, ntrip_client_pop_rtcm(&client, NULL, 8));
    TEST_ASSERT_EQUAL(0, ntrip_client_pop_rtcm(&client, buf, 0));
}

void test_config_set_defaults_null(void)
{
    ntrip_client_config_set_defaults(NULL);
    TEST_PASS();
}

/* ==========================================================================
 * Runner
 * ========================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* State names */
    RUN_TEST(test_state_names_all_states);

    /* Init / lifecycle */
    RUN_TEST(test_init_resets_to_idle);
    RUN_TEST(test_init_null_does_not_crash);
    RUN_TEST(test_start_from_idle_to_connecting);
    RUN_TEST(test_start_from_non_idle_fails);
    RUN_TEST(test_start_null_returns_false);
    RUN_TEST(test_stop_returns_to_idle);
    RUN_TEST(test_stop_from_idle_fails);
    RUN_TEST(test_stop_from_streaming_works);

    /* State transitions */
    RUN_TEST(test_valid_transitions);
    RUN_TEST(test_invalid_transitions_rejected);

    /* Request generation */
    RUN_TEST(test_request_contains_correct_mountpoint);
    RUN_TEST(test_request_contains_http_10);
    RUN_TEST(test_request_contains_user_agent);
    RUN_TEST(test_request_contains_ntrip_version);
    RUN_TEST(test_request_contains_connection_close);

    /* Basic Auth */
    RUN_TEST(test_basic_auth_in_request_when_credentials_set);
    RUN_TEST(test_basic_auth_correct_encoding_empty_password);
    RUN_TEST(test_no_basic_auth_when_no_username);
    RUN_TEST(test_custom_user_agent);

    /* ICY 200 / HTTP 200 → STREAMING */
    RUN_TEST(test_icy_200_ok_transitions_to_streaming);
    RUN_TEST(test_http_10_200_transitions_to_streaming);
    RUN_TEST(test_http_11_200_transitions_to_streaming);
    RUN_TEST(test_streaming_resets_reconnect_count);

    /* HTTP errors */
    RUN_TEST(test_http_401_goes_to_error);
    RUN_TEST(test_http_403_goes_to_error);
    RUN_TEST(test_http_404_goes_to_error);
    RUN_TEST(test_error_then_retry);

    /* RTCM data forwarding */
    RUN_TEST(test_rtcm_bytes_landed_in_rtcm_buffer);
    RUN_TEST(test_rtcm_data_after_headers_in_response);
    RUN_TEST(test_no_rtcm_when_not_streaming);

    /* Reconnect / backoff */
    RUN_TEST(test_reconnect_after_error);
    RUN_TEST(test_exponential_backoff_doubles);
    RUN_TEST(test_backoff_capped_at_max);

    /* Timeout */
    RUN_TEST(test_connect_timeout_goes_to_error);

    /* Error cases */
    RUN_TEST(test_no_transport_goes_to_error);
    RUN_TEST(test_no_config_goes_to_error);
    RUN_TEST(test_disconnect_during_streaming_goes_to_error);

    /* Edge cases / null safety */
    RUN_TEST(test_service_step_null_does_not_crash);
    RUN_TEST(test_service_step_not_started_does_nothing);
    RUN_TEST(test_configure_null_returns_error);
    RUN_TEST(test_pop_rtcm_null_safety);
    RUN_TEST(test_config_set_defaults_null);

    return UNITY_END();
}
