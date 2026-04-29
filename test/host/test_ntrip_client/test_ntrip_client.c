#include "unity.h"
#include "ntrip_client.h"
#include "byte_ring_buffer.h"
#include <string.h>

static ntrip_client_t client;
static uint8_t tcp_storage[256];
static byte_ring_buffer_t tcp_source;

void setUp(void)
{
    memset(&client, 0, sizeof(client));
    ntrip_client_init(&client);

    memset(tcp_storage, 0, sizeof(tcp_storage));
    byte_ring_buffer_init(&tcp_source, tcp_storage, sizeof(tcp_source));
    ntrip_client_set_tcp_source(&client, &tcp_source);
}

void tearDown(void) {}

/* ---- State name ---- */

void test_state_name_returns_correct_string(void)
{
    TEST_ASSERT_EQUAL_STRING("idle", ntrip_client_state_name(NTRIP_STATE_IDLE));
    TEST_ASSERT_EQUAL_STRING("connecting", ntrip_client_state_name(NTRIP_STATE_CONNECTING));
    TEST_ASSERT_EQUAL_STRING("authenticating", ntrip_client_state_name(NTRIP_STATE_AUTHENTICATING));
    TEST_ASSERT_EQUAL_STRING("connected", ntrip_client_state_name(NTRIP_STATE_CONNECTED));
    TEST_ASSERT_EQUAL_STRING("error", ntrip_client_state_name(NTRIP_STATE_ERROR));
    TEST_ASSERT_EQUAL_STRING("reconnect", ntrip_client_state_name(NTRIP_STATE_RECONNECT));
    TEST_ASSERT_EQUAL_STRING("unknown", ntrip_client_state_name((ntrip_state_t)99));
}

/* ---- Init ---- */

void test_init_resets_to_idle(void)
{
    TEST_ASSERT_EQUAL(NTRIP_STATE_IDLE, ntrip_client_get_state(&client));
    TEST_ASSERT_FALSE(ntrip_client_is_started(&client));
    TEST_ASSERT_EQUAL(0, ntrip_client_get_reconnect_count(&client));
}

void test_init_null_does_not_crash(void)
{
    ntrip_client_init(NULL);
    TEST_PASS();
}

/* ---- Start ---- */

void test_start_transitions_to_connecting(void)
{
    bool ok = ntrip_client_start(&client);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTING, ntrip_client_get_state(&client));
    TEST_ASSERT_TRUE(ntrip_client_is_started(&client));
}

void test_start_from_non_idle_fails(void)
{
    ntrip_client_start(&client); /* IDLE -> CONNECTING */
    bool ok = ntrip_client_start(&client); /* CONNECTING -> should fail */
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
    /* IDLE -> CONNECTING */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_CONNECTING, 100));

    /* CONNECTING -> AUTHENTICATING */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_AUTHENTICATING, 200));

    /* AUTHENTICATING -> CONNECTED */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_CONNECTED, 300));
    TEST_ASSERT_EQUAL(0, ntrip_client_get_reconnect_count(&client));

    /* CONNECTED -> ERROR */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_ERROR, 400));

    /* ERROR -> RECONNECT */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_RECONNECT, 500));
    TEST_ASSERT_EQUAL(1, ntrip_client_get_reconnect_count(&client));

    /* RECONNECT -> CONNECTING */
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_CONNECTING, 600));
}

void test_invalid_transitions_rejected(void)
{
    /* IDLE -> AUTHENTICATING (must go through CONNECTING first) */
    TEST_ASSERT_FALSE(ntrip_client_transition(&client, NTRIP_STATE_AUTHENTICATING, 100));

    /* IDLE -> CONNECTED (must go through CONNECTING and AUTHENTICATING) */
    TEST_ASSERT_FALSE(ntrip_client_transition(&client, NTRIP_STATE_CONNECTED, 100));

    /* IDLE -> ERROR (must go through connection first) */
    TEST_ASSERT_FALSE(ntrip_client_transition(&client, NTRIP_STATE_ERROR, 100));

    /* Test from CONNECTING */
    ntrip_client_transition(&client, NTRIP_STATE_CONNECTING, 100);
    /* CONNECTING -> CONNECTED (skips AUTHENTICATING) */
    TEST_ASSERT_FALSE(ntrip_client_transition(&client, NTRIP_STATE_CONNECTED, 200));
}

void test_transition_null_returns_false(void)
{
    TEST_ASSERT_FALSE(ntrip_client_transition(NULL, NTRIP_STATE_CONNECTING, 0));
}

void test_same_state_transition_allowed(void)
{
    TEST_ASSERT_TRUE(ntrip_client_transition(&client, NTRIP_STATE_IDLE, 100));
    /* Staying in IDLE is valid (self-transition) */
}

/* ---- Skeleton state machine progression ---- */

void test_skeleton_auto_progresses_to_connected(void)
{
    ntrip_client_start(&client);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTING, ntrip_client_get_state(&client));

    /* Simulate time passing: CONNECTING -> AUTHENTICATING (after 1s) */
    ntrip_client_service_step((runtime_component_t*)&client, 1500000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_AUTHENTICATING, ntrip_client_get_state(&client));

    /* Simulate time passing: AUTHENTICATING -> CONNECTED (after another 1s) */
    ntrip_client_service_step((runtime_component_t*)&client, 3000000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTED, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(0, ntrip_client_get_reconnect_count(&client));
}

void test_skeleton_error_auto_reconnects(void)
{
    ntrip_client_start(&client);

    /* Fast-forward to CONNECTED */
    ntrip_client_service_step((runtime_component_t*)&client, 1500000);
    ntrip_client_service_step((runtime_component_t*)&client, 3000000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTED, ntrip_client_get_state(&client));

    /* Force error */
    ntrip_client_transition(&client, NTRIP_STATE_ERROR, 3000000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_ERROR, ntrip_client_get_state(&client));

    /* ERROR -> RECONNECT -> CONNECTING after timeout */
    ntrip_client_service_step((runtime_component_t*)&client, 5000000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTING, ntrip_client_get_state(&client));
    TEST_ASSERT_EQUAL(1, ntrip_client_get_reconnect_count(&client));
}

/* ---- RTCM data forwarding ---- */

void test_rtcm_forwarding_when_connected(void)
{
    ntrip_client_start(&client);

    /* Fast-forward to CONNECTED */
    ntrip_client_service_step((runtime_component_t*)&client, 1500000);
    ntrip_client_service_step((runtime_component_t*)&client, 3000000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_CONNECTED, ntrip_client_get_state(&client));

    /* Push data into TCP source */
    uint8_t rtcm_data[] = {0xD3, 0x00, 0x13, 0x3E, 0xD7, 0xD3, 0x02, 0x02};
    byte_ring_buffer_write(&tcp_source, rtcm_data, sizeof(rtcm_data));

    /* Service step should forward TCP data to RTCM buffer */
    ntrip_client_service_step((runtime_component_t*)&client, 3001000);

    TEST_ASSERT_EQUAL(sizeof(rtcm_data), ntrip_client_rtcm_available(&client));

    /* Pop RTCM data */
    uint8_t out[16] = {0};
    size_t popped = ntrip_client_pop_rtcm(&client, out, sizeof(out));
    TEST_ASSERT_EQUAL(sizeof(rtcm_data), popped);
    TEST_ASSERT_EQUAL_MEMORY(rtcm_data, out, sizeof(rtcm_data));
    TEST_ASSERT_EQUAL(0, ntrip_client_rtcm_available(&client));
}

void test_no_rtcm_forwarding_when_not_connected(void)
{
    ntrip_client_start(&client);
    /* Still in CONNECTING state */

    uint8_t data[] = {0xD3, 0x00};
    byte_ring_buffer_write(&tcp_source, data, sizeof(data));

    ntrip_client_service_step((runtime_component_t*)&client, 500000);
    TEST_ASSERT_EQUAL(0, ntrip_client_rtcm_available(&client));
}

/* ---- Service step edge cases ---- */

void test_service_step_null_does_not_crash(void)
{
    ntrip_client_service_step(NULL, 1000);
    TEST_PASS();
}

void test_service_step_not_started_does_nothing(void)
{
    /* Client is initialized but never started */
    ntrip_client_service_step((runtime_component_t*)&client, 1000000);
    TEST_ASSERT_EQUAL(NTRIP_STATE_IDLE, ntrip_client_get_state(&client));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_state_name_returns_correct_string);
    RUN_TEST(test_init_resets_to_idle);
    RUN_TEST(test_init_null_does_not_crash);
    RUN_TEST(test_start_transitions_to_connecting);
    RUN_TEST(test_start_from_non_idle_fails);
    RUN_TEST(test_start_null_returns_false);
    RUN_TEST(test_valid_transitions);
    RUN_TEST(test_invalid_transitions_rejected);
    RUN_TEST(test_transition_null_returns_false);
    RUN_TEST(test_same_state_transition_allowed);
    RUN_TEST(test_skeleton_auto_progresses_to_connected);
    RUN_TEST(test_skeleton_error_auto_reconnects);
    RUN_TEST(test_rtcm_forwarding_when_connected);
    RUN_TEST(test_no_rtcm_forwarding_when_not_connected);
    RUN_TEST(test_service_step_null_does_not_crash);
    RUN_TEST(test_service_step_not_started_does_nothing);
    return UNITY_END();
}
