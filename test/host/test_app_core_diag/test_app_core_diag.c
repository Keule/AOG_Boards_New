/* ========================================================================
 * test_app_core_diag — Host test for app_core_nav_diag_step (Pflicht 3)
 *
 * Tests the pure diagnostic step function extracted from app_core.c.
 * Since app_core.c depends on ESP_LOG and many ESP32 headers, this test
 * provides a local implementation of app_core_nav_diag_step() that links
 * against nav_diagnostics components directly.
 *
 * The test verifies:
 *  1. Collector init + wiring + diag_step → snapshot populated
 *  2. diag_step with ETH link update works
 *  3. diag_step returns true when recovery needed
 *  4. diag_step returns false when all healthy
 *  5. Multiple calls don't crash (non-blocking, no state corruption)
 *  6. NULL safety
 *  7. GNSS stale detection
 *  8. UART error detection
 * ======================================================================== */

#include "unity.h"
#include "nav_diagnostics.h"
#include "transport_uart.h"
#include "transport_tcp.h"
#include "gnss_um980.h"
#include "gnss_dual_heading.h"
#include "ntrip_client.h"
#include "rtcm_router.h"
#include "aog_navigation_app.h"
#include "byte_ring_buffer.h"
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* ---- Local implementation of app_core_nav_diag_step ----
 * Identical to the one in app_core.c, but without ESP-IDF dependencies.
 * This is the function-under-test for Pflicht 3. */

bool app_core_nav_diag_step(void* collector,
                             void* snapshot,
                             void* recovery,
                             uint64_t now_ms,
                             bool eth_link_up)
{
    if (collector == NULL || snapshot == NULL || recovery == NULL) {
        return false;
    }

    nav_health_collector_t* coll    = (nav_health_collector_t*)collector;
    nav_health_snapshot_t*  snap    = (nav_health_snapshot_t*)snapshot;
    nav_recovery_status_t*  recov   = (nav_recovery_status_t*)recovery;

    nav_health_collector_set_eth_link(coll, eth_link_up);
    nav_health_collect(coll, snap, now_ms);
    nav_recovery_evaluate(snap, recov);

    return nav_recovery_needs_action(recov);
}

/* ---- Test fixtures ---- */

static nav_health_collector_t s_coll;
static nav_health_snapshot_t  s_snap;
static nav_recovery_status_t  s_recovery;
static transport_uart_t       s_uart_primary;
static transport_uart_t       s_uart_secondary;
static gnss_um980_t           s_gnss_primary;
static gnss_um980_t           s_gnss_secondary;
static gnss_dual_heading_calc_t s_heading;
static ntrip_client_t         s_ntrip;
static rtcm_router_t          s_rtcm;
static aog_nav_app_t          s_aog;
static transport_tcp_t        s_tcp;

void setUp(void)
{
    memset(&s_coll, 0, sizeof(s_coll));
    memset(&s_snap, 0, sizeof(s_snap));
    memset(&s_recovery, 0, sizeof(s_recovery));
    memset(&s_uart_primary, 0, sizeof(s_uart_primary));
    memset(&s_uart_secondary, 0, sizeof(s_uart_secondary));
    memset(&s_gnss_primary, 0, sizeof(s_gnss_primary));
    memset(&s_gnss_secondary, 0, sizeof(s_gnss_secondary));
    memset(&s_heading, 0, sizeof(s_heading));
    memset(&s_ntrip, 0, sizeof(s_ntrip));
    memset(&s_rtcm, 0, sizeof(s_rtcm));
    memset(&s_aog, 0, sizeof(s_aog));
    memset(&s_tcp, 0, sizeof(s_tcp));
    nav_diag_log_stats_reset();
}

void tearDown(void) {}

/* ---- Helper: Wire all 10 subsystems ---- */

static void wire_all_subsystems(void)
{
    byte_ring_buffer_init(&s_uart_primary.rx_buffer, s_uart_primary.rx_storage, sizeof(s_uart_primary.rx_storage));
    byte_ring_buffer_init(&s_uart_primary.tx_buffer, s_uart_primary.tx_storage, sizeof(s_uart_primary.tx_storage));
    byte_ring_buffer_init(&s_uart_secondary.rx_buffer, s_uart_secondary.rx_storage, sizeof(s_uart_secondary.rx_storage));
    byte_ring_buffer_init(&s_uart_secondary.tx_buffer, s_uart_secondary.tx_storage, sizeof(s_uart_secondary.tx_storage));
    byte_ring_buffer_init(&s_tcp.rx_buffer, s_tcp.rx_storage, sizeof(s_tcp.rx_storage));
    byte_ring_buffer_init(&s_tcp.tx_buffer, s_tcp.tx_storage, sizeof(s_tcp.tx_storage));

    gnss_um980_init(&s_gnss_primary, 0, "GNSS1");
    gnss_um980_init(&s_gnss_secondary, 1, "GNSS2");
    gnss_dual_heading_init(&s_heading);
    ntrip_client_init(&s_ntrip);
    rtcm_router_init(&s_rtcm);

    nav_health_collector_init(&s_coll);
    nav_health_collector_set_uart_primary(&s_coll, &s_uart_primary);
    nav_health_collector_set_uart_secondary(&s_coll, &s_uart_secondary);
    nav_health_collector_set_gnss_primary(&s_coll, &s_gnss_primary);
    nav_health_collector_set_gnss_secondary(&s_coll, &s_gnss_secondary);
    nav_health_collector_set_heading(&s_coll, &s_heading);
    nav_health_collector_set_ntrip(&s_coll, &s_ntrip);
    nav_health_collector_set_rtcm_router(&s_coll, &s_rtcm);
    nav_health_collector_set_aog_nav_app(&s_coll, &s_aog);
    nav_health_collector_set_tcp(&s_coll, &s_tcp);
}

/* ====================================================================
 * Test 1: Collector init + wiring + diag_step → snapshot populated
 * ==================================================================== */

void test_diag_step_populates_snapshot(void)
{
    wire_all_subsystems();

    s_uart_primary.stats.rx_bytes_in = 500;
    s_gnss_primary.snapshot.valid = true;
    s_gnss_primary.snapshot.fresh = true;
    s_gnss_primary.sentences_parsed = 100;
    s_ntrip.state = NTRIP_STATE_CONNECTED;
    s_tcp.connected = true;
    s_heading.result.valid = true;
    s_heading.calc_count = 5;
    s_aog.output_state = AOG_OUTPUT_OK;
    s_aog.pgn214_send_count = 42;

    bool needs = app_core_nav_diag_step(
        &s_coll, &s_snap, &s_recovery, 60000, true);

    TEST_ASSERT_FALSE(needs);
    TEST_ASSERT_EQUAL(500, s_snap.uart_primary_rx_bytes);
    TEST_ASSERT_TRUE(s_snap.gnss_primary_valid);
    TEST_ASSERT_TRUE(s_snap.gnss_primary_fresh);
    TEST_ASSERT_EQUAL(100, s_snap.gnss_primary_sentences);
    TEST_ASSERT_EQUAL(NAV_NTRIP_STATE_CONNECTED, s_snap.ntrip_state);
    TEST_ASSERT_TRUE(s_snap.tcp_connected);
    TEST_ASSERT_TRUE(s_snap.heading_valid);
    TEST_ASSERT_EQUAL(5, s_snap.heading_calc_count);
    TEST_ASSERT_EQUAL(NAV_AOG_STATE_OK, s_snap.aog_output_state);
    TEST_ASSERT_EQUAL(42, s_snap.aog_tx_frames);
    TEST_ASSERT_TRUE(s_snap.eth_link_up);
    TEST_ASSERT_EQUAL(60000, (int)s_snap.uptime_ms);
}

/* ====================================================================
 * Test 2: ETH link update
 * ==================================================================== */

void test_diag_step_eth_link_update(void)
{
    wire_all_subsystems();

    app_core_nav_diag_step(&s_coll, &s_snap, &s_recovery, 1000, true);
    TEST_ASSERT_TRUE(s_snap.eth_link_up);

    app_core_nav_diag_step(&s_coll, &s_snap, &s_recovery, 2000, false);
    TEST_ASSERT_FALSE(s_snap.eth_link_up);
    TEST_ASSERT_TRUE(s_recovery.eth_link_down);
    TEST_ASSERT_TRUE(s_recovery.actions & NAV_RECOVERY_ETH_REINIT);
}

/* ====================================================================
 * Test 3: Returns true when recovery needed
 * ==================================================================== */

void test_diag_step_returns_true_on_recovery(void)
{
    wire_all_subsystems();
    s_ntrip.state = NTRIP_STATE_ERROR;
    s_ntrip.reconnect_count = 3;
    s_tcp.connected = false;

    bool needs = app_core_nav_diag_step(
        &s_coll, &s_snap, &s_recovery, 10000, true);

    TEST_ASSERT_TRUE(needs);
    TEST_ASSERT_TRUE(s_recovery.ntrip_should_reconnect);
    TEST_ASSERT_TRUE(s_recovery.tcp_disconnected);
    TEST_ASSERT_TRUE(s_recovery.actions & NAV_RECOVERY_NTRIP_RECONNECT);
    TEST_ASSERT_TRUE(s_recovery.actions & NAV_RECOVERY_TCP_RECONNECT);
}

/* ====================================================================
 * Test 4: Returns false when all healthy
 * ==================================================================== */

void test_diag_step_returns_false_when_healthy(void)
{
    wire_all_subsystems();
    s_ntrip.state = NTRIP_STATE_CONNECTED;
    s_tcp.connected = true;
    s_gnss_primary.snapshot.valid = true;
    s_gnss_primary.snapshot.fresh = true;
    s_gnss_secondary.snapshot.valid = true;
    s_gnss_secondary.snapshot.fresh = true;

    bool needs = app_core_nav_diag_step(
        &s_coll, &s_snap, &s_recovery, 5000, true);

    TEST_ASSERT_FALSE(needs);
    TEST_ASSERT_EQUAL(NAV_RECOVERY_NONE, s_recovery.actions);
}

/* ====================================================================
 * Test 5: Multiple calls — non-blocking safety
 * ==================================================================== */

void test_diag_step_multiple_calls_safe(void)
{
    wire_all_subsystems();
    s_ntrip.state = NTRIP_STATE_RETRY_WAIT;
    s_gnss_primary.snapshot.fresh = false;
    s_gnss_primary.bytes_received = 10000;

    for (int i = 0; i < 100; i++) {
        bool needs = app_core_nav_diag_step(
            &s_coll, &s_snap, &s_recovery,
            (uint64_t)(i * 100), true);
        TEST_ASSERT_TRUE(needs || s_recovery.actions != NAV_RECOVERY_NONE);
    }
}

/* ====================================================================
 * Test 6: NULL safety
 * ==================================================================== */

void test_diag_step_null_safety(void)
{
    bool needs = app_core_nav_diag_step(NULL, NULL, NULL, 0, false);
    TEST_ASSERT_FALSE(needs);

    nav_health_snapshot_t snap;
    nav_recovery_status_t recov;
    needs = app_core_nav_diag_step(NULL, &snap, &recov, 0, false);
    TEST_ASSERT_FALSE(needs);

    nav_health_collector_t coll;
    needs = app_core_nav_diag_step(&coll, NULL, &recov, 0, false);
    TEST_ASSERT_FALSE(needs);

    needs = app_core_nav_diag_step(&coll, &snap, NULL, 0, false);
    TEST_ASSERT_FALSE(needs);
}

/* ====================================================================
 * Test 7: GNSS stale detection
 * ==================================================================== */

void test_diag_step_gnss_stale(void)
{
    wire_all_subsystems();
    s_gnss_primary.snapshot.fresh = false;
    s_gnss_primary.bytes_received = 50000;

    bool needs = app_core_nav_diag_step(
        &s_coll, &s_snap, &s_recovery, 30000, true);

    TEST_ASSERT_TRUE(needs);
    TEST_ASSERT_TRUE(s_recovery.gnss_primary_stale);
    TEST_ASSERT_TRUE(s_recovery.actions & NAV_RECOVERY_GNSS_RESET);
}

/* ====================================================================
 * Test 8: UART error detection
 * ==================================================================== */

void test_diag_step_uart_errors(void)
{
    wire_all_subsystems();
    s_uart_primary.stats.rx_overflow_count = 10;
    s_uart_secondary.stats.tx_errors = 3;

    bool needs = app_core_nav_diag_step(
        &s_coll, &s_snap, &s_recovery, 0, true);

    TEST_ASSERT_TRUE(needs);
    TEST_ASSERT_TRUE(s_recovery.uart_primary_errors);
    TEST_ASSERT_TRUE(s_recovery.uart_secondary_errors);
    TEST_ASSERT_TRUE(s_recovery.actions & NAV_RECOVERY_UART_CHECK);
    TEST_ASSERT_EQUAL(10, (int)s_snap.uart_primary_rx_overflows);
    TEST_ASSERT_EQUAL(3, (int)s_snap.uart_secondary_tx_errors);
}

/* ====================================================================
 * TEST RUNNER
 * ==================================================================== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_diag_step_populates_snapshot);
    RUN_TEST(test_diag_step_eth_link_update);
    RUN_TEST(test_diag_step_returns_true_on_recovery);
    RUN_TEST(test_diag_step_returns_false_when_healthy);
    RUN_TEST(test_diag_step_multiple_calls_safe);
    RUN_TEST(test_diag_step_null_safety);
    RUN_TEST(test_diag_step_gnss_stale);
    RUN_TEST(test_diag_step_uart_errors);

    return UNITY_END();
}
