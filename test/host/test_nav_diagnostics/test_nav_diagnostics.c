/* ========================================================================
 * test_nav_diagnostics — Host tests for NAV-DIAG-001
 *
 * Test cases (Teil 4):
 *
 * HEALTH SNAPSHOT (Teil 1):
 *  1.  Init collector — all NULL, zero fields
 *  2.  Collect with NULL collector — zeros + uptime only
 *  3.  Collect with NULL subsystems — graceful zeros
 *  4.  Collect with UART primary — rx/tx bytes, overflows, errors
 *  5.  Collect with GNSS primary — valid/fresh, sentences, checksum err, timeout
 *  6.  Collect with GNSS secondary — same as primary
 *  7.  Collect with heading — valid + calc_count
 *  8.  Collect with NTRIP — state mapping, reconnect count, error, http status
 *  9.  Collect with RTCM router — bytes_in/out/dropped, overflows
 * 10.  Collect with AOG nav app — output state, tx frames, suppressed
 * 11.  Collect with TCP — connected, rx/tx bytes
 * 12.  Collect with ETH link — link_up from collector
 * 13.  Error recording — total_errors, last_error_module/code
 * 14.  Full snapshot with all subsystems — all fields populated
 *
 * RATE-LIMITED LOGGING (Teil 2):
 * 15.  Log entry init — default interval for level
 * 16.  Log entry init_ex — custom interval
 * 17.  Log emit — first message always passes
 * 18.  Log emit rate-limited — second message within interval suppressed
 * 19.  Log emit after interval — message passes again
 * 20.  Log emit suppressed count tracking
 * 21.  Log stats — total_emitted, total_suppressed, total_errors
 * 22.  Log level names — DBG, INF, WRN, ERR
 * 23.  Error counter increment
 *
 * RECOVERY EVALUATION (Teil 3):
 * 24.  Recovery evaluate — all healthy → no actions
 * 25.  NTRIP disconnect → reconnect flag
 * 26.  TCP disconnect → reconnect flag
 * 27.  ETH link down → reinit flag
 * 28.  GNSS stale → reset flag
 * 29.  UART errors → check flag
 * 30.  Multiple simultaneous issues → combined flags
 * 31.  Recovery needs action — true/false
 *
 * STATE NAME HELPERS:
 * 32.  NTRIP state names
 * 33.  AOG state names
 * 34.  Error module names
 * 35.  Recovery flag names
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

/* ---- Test fixtures ---- */

static nav_health_collector_t s_coll;
static nav_health_snapshot_t  s_snap;
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
    memset(&s_uart_primary, 0, sizeof(s_uart_primary));
    memset(&s_uart_secondary, 0, sizeof(s_uart_secondary));
    memset(&s_gnss_primary, 0, sizeof(s_gnss_primary));
    memset(&s_gnss_secondary, 0, sizeof(s_gnss_secondary));
    memset(&s_heading, 0, sizeof(s_heading));
    memset(&s_ntrip, 0, sizeof(s_ntrip));
    memset(&s_rtcm, 0, sizeof(s_rtcm));
    memset(&s_aog, 0, sizeof(s_aog));
    memset(&s_tcp, 0, sizeof(s_tcp));
}

void tearDown(void) {}

/* ====================================================================
 * HEALTH SNAPSHOT TESTS (1-14)
 * ==================================================================== */

/* Test 1: Init collector */
void test_health_init_collector(void)
{
    nav_health_collector_init(&s_coll);
    TEST_ASSERT_NULL(s_coll.uart_primary);
    TEST_ASSERT_NULL(s_coll.uart_secondary);
    TEST_ASSERT_NULL(s_coll.gnss_primary);
    TEST_ASSERT_NULL(s_coll.gnss_secondary);
    TEST_ASSERT_NULL(s_coll.heading);
    TEST_ASSERT_NULL(s_coll.ntrip);
    TEST_ASSERT_NULL(s_coll.rtcm_router);
    TEST_ASSERT_NULL(s_coll.aog_nav_app);
    TEST_ASSERT_NULL(s_coll.tcp);
    TEST_ASSERT_EQUAL(0, s_coll.total_errors);
    TEST_ASSERT_EQUAL(0, s_coll.last_error_module);
    TEST_ASSERT_EQUAL(0, s_coll.last_error_code);
}

/* Test 2: Collect with NULL collector */
void test_health_collect_null_collector(void)
{
    nav_health_collect(NULL, &s_snap, 12345);
    /* Should only have uptime, everything else zero */
    TEST_ASSERT_EQUAL(12345, (int)s_snap.uptime_ms);
    TEST_ASSERT_FALSE(s_snap.gnss_primary_valid);
    TEST_ASSERT_FALSE(s_snap.eth_link_up);
    TEST_ASSERT_EQUAL(0, s_snap.total_errors);
}

/* Test 3: Collect with NULL subsystems — graceful zeros */
void test_health_collect_null_subsystems(void)
{
    nav_health_collector_init(&s_coll);
    nav_health_collect(&s_coll, &s_snap, 99999);
    TEST_ASSERT_EQUAL(99999, (int)s_snap.uptime_ms);
    TEST_ASSERT_EQUAL(0, s_snap.uart_primary_rx_bytes);
    TEST_ASSERT_FALSE(s_snap.gnss_primary_valid);
    TEST_ASSERT_FALSE(s_snap.heading_valid);
    TEST_ASSERT_EQUAL(0, s_snap.ntrip_reconnect_count);
    TEST_ASSERT_EQUAL(0, s_snap.rtcm_bytes_in);
    TEST_ASSERT_EQUAL(0, s_snap.aog_tx_frames);
    TEST_ASSERT_FALSE(s_snap.tcp_connected);
    TEST_ASSERT_FALSE(s_snap.eth_link_up);
}

/* Test 4: Collect with UART primary */
void test_health_collect_uart_primary(void)
{
    transport_uart_config_t uart_cfg = { .port = BOARD_UART_PORT_PRIMARY, .baudrate = 115200 };
    transport_uart_init(&s_uart_primary, &uart_cfg);

    /* Simulate some stats */
    s_uart_primary.stats.rx_bytes_in = 1000;
    s_uart_primary.stats.tx_bytes_out = 500;
    s_uart_primary.stats.rx_overflow_count = 3;
    s_uart_primary.stats.tx_errors = 1;

    nav_health_collector_init(&s_coll);
    nav_health_collector_set_uart_primary(&s_coll, &s_uart_primary);
    nav_health_collect(&s_coll, &s_snap, 0);

    TEST_ASSERT_EQUAL(1000, s_snap.uart_primary_rx_bytes);
    TEST_ASSERT_EQUAL(500, s_snap.uart_primary_tx_bytes);
    TEST_ASSERT_EQUAL(3, s_snap.uart_primary_rx_overflows);
    TEST_ASSERT_EQUAL(1, s_snap.uart_primary_tx_errors);
    /* Secondary should be zero */
    TEST_ASSERT_EQUAL(0, s_snap.uart_secondary_rx_bytes);
}

/* Test 5: Collect with GNSS primary */
void test_health_collect_gnss_primary(void)
{
    gnss_um980_init(&s_gnss_primary, NULL, BOARD_UART_PORT_PRIMARY);
    s_gnss_primary.snapshot.valid = true;
    s_gnss_primary.snapshot.fresh = true;
    s_gnss_primary.sentences_parsed = 500;
    s_gnss_primary.checksum_errors = 2;
    s_gnss_primary.timeout_events = 1;
    s_gnss_primary.bytes_received = 25000;

    nav_health_collector_init(&s_coll);
    nav_health_collector_set_gnss_primary(&s_coll, &s_gnss_primary);
    nav_health_collect(&s_coll, &s_snap, 0);

    TEST_ASSERT_TRUE(s_snap.gnss_primary_valid);
    TEST_ASSERT_TRUE(s_snap.gnss_primary_fresh);
    TEST_ASSERT_EQUAL(500, s_snap.gnss_primary_sentences);
    TEST_ASSERT_EQUAL(2, s_snap.gnss_primary_checksum_err);
    TEST_ASSERT_EQUAL(1, s_snap.gnss_primary_timeout_events);
    TEST_ASSERT_EQUAL(25000, s_snap.gnss_primary_bytes);
}

/* Test 6: Collect with GNSS secondary */
void test_health_collect_gnss_secondary(void)
{
    gnss_um980_init(&s_gnss_secondary, NULL, BOARD_UART_PORT_SECONDARY);
    s_gnss_secondary.snapshot.valid = true;
    s_gnss_secondary.snapshot.fresh = false;
    s_gnss_secondary.sentences_parsed = 300;

    nav_health_collector_init(&s_coll);
    nav_health_collector_set_gnss_secondary(&s_coll, &s_gnss_secondary);
    nav_health_collect(&s_coll, &s_snap, 0);

    TEST_ASSERT_TRUE(s_snap.gnss_secondary_valid);
    TEST_ASSERT_FALSE(s_snap.gnss_secondary_fresh);
    TEST_ASSERT_EQUAL(300, s_snap.gnss_secondary_sentences);
}

/* Test 7: Collect with heading */
void test_health_collect_heading(void)
{
    gnss_dual_heading_init(&s_heading);
    s_heading.result.valid = true;
    s_heading.calc_count = 42;

    nav_health_collector_init(&s_coll);
    nav_health_collector_set_heading(&s_coll, &s_heading);
    nav_health_collect(&s_coll, &s_snap, 0);

    TEST_ASSERT_TRUE(s_snap.heading_valid);
    TEST_ASSERT_EQUAL(42, s_snap.heading_calc_count);
}

/* Test 8: Collect with NTRIP */
void test_health_collect_ntrip(void)
{
    ntrip_client_init(&s_ntrip);
    s_ntrip.state = NTRIP_STATE_CONNECTED;
    s_ntrip.reconnect_count = 3;
    s_ntrip.last_error = NTRIP_OK;
    s_ntrip.http_status_code = 200;

    nav_health_collector_init(&s_coll);
    nav_health_collector_set_ntrip(&s_coll, &s_ntrip);
    nav_health_collect(&s_coll, &s_snap, 0);

    TEST_ASSERT_EQUAL(NAV_NTRIP_STATE_CONNECTED, s_snap.ntrip_state);
    TEST_ASSERT_EQUAL(3, s_snap.ntrip_reconnect_count);
    TEST_ASSERT_EQUAL(0, s_snap.ntrip_last_error);
    TEST_ASSERT_EQUAL(200, s_snap.ntrip_http_status);
}

/* Test 9: Collect with RTCM router */
void test_health_collect_rtcm_router(void)
{
    rtcm_router_init(&s_rtcm);
    /* Directly set passthrough stats */
    s_rtcm.passthrough.stats.bytes_in = 10000;
    s_rtcm.passthrough.stats.bytes_out = 9500;
    s_rtcm.passthrough.stats.bytes_dropped = 500;
    s_rtcm.output_overflow_count = 7;

    nav_health_collector_init(&s_coll);
    nav_health_collector_set_rtcm_router(&s_coll, &s_rtcm);
    nav_health_collect(&s_coll, &s_snap, 0);

    TEST_ASSERT_EQUAL(10000, s_snap.rtcm_bytes_in);
    TEST_ASSERT_EQUAL(9500, s_snap.rtcm_bytes_out);
    TEST_ASSERT_EQUAL(500, s_snap.rtcm_bytes_dropped);
    TEST_ASSERT_EQUAL(7, s_snap.rtcm_output_overflows);
}

/* Test 10: Collect with AOG nav app */
void test_health_collect_aog_nav_app(void)
{
    /* Init aog_nav_app manually (memset is sufficient for test) */
    s_aog.output_state = AOG_OUTPUT_OK;
    s_aog.pgn214_send_count = 1000;
    s_aog.suppress_count = 5;
    s_aog.hello_send_count = 2;
    s_aog.scan_send_count = 1;

    nav_health_collector_init(&s_coll);
    nav_health_collector_set_aog_nav_app(&s_coll, &s_aog);
    nav_health_collect(&s_coll, &s_snap, 0);

    TEST_ASSERT_EQUAL(NAV_AOG_STATE_OK, s_snap.aog_output_state);
    TEST_ASSERT_EQUAL(1000, s_snap.aog_tx_frames);
    TEST_ASSERT_EQUAL(5, s_snap.aog_suppressed);
    TEST_ASSERT_EQUAL(2, s_snap.aog_hello_count);
    TEST_ASSERT_EQUAL(1, s_snap.aog_scan_count);
}

/* Test 11: Collect with TCP */
void test_health_collect_tcp(void)
{
    transport_tcp_config_t tcp_cfg = { .remote_ip = 0x01020304, .remote_port = 2101 };
    transport_tcp_init(&s_tcp, &tcp_cfg);
    s_tcp.connected = true;
    byte_ring_buffer_write(&s_tcp.rx_buffer, (const uint8_t*)"AB", 2);
    byte_ring_buffer_write(&s_tcp.tx_buffer, (const uint8_t*)"CD", 2);

    nav_health_collector_init(&s_coll);
    nav_health_collector_set_tcp(&s_coll, &s_tcp);
    nav_health_collect(&s_coll, &s_snap, 0);

    TEST_ASSERT_TRUE(s_snap.tcp_connected);
    TEST_ASSERT_EQUAL(2, s_snap.tcp_rx_bytes);
    TEST_ASSERT_EQUAL(2, s_snap.tcp_tx_bytes);
}

/* Test 12: Collect with ETH link */
void test_health_collect_eth_link(void)
{
    nav_health_collector_init(&s_coll);
    nav_health_collector_set_eth_link(&s_coll, true);
    nav_health_collect(&s_coll, &s_snap, 0);

    TEST_ASSERT_TRUE(s_snap.eth_link_up);

    nav_health_collector_set_eth_link(&s_coll, false);
    nav_health_collect(&s_coll, &s_snap, 0);

    TEST_ASSERT_FALSE(s_snap.eth_link_up);
}

/* Test 13: Error recording */
void test_health_error_recording(void)
{
    nav_health_collector_init(&s_coll);

    nav_health_record_error(&s_coll, NAV_ERR_MODULE_GNSS_PRIMARY, 1);
    TEST_ASSERT_EQUAL(1, s_coll.total_errors);
    TEST_ASSERT_EQUAL(NAV_ERR_MODULE_GNSS_PRIMARY, s_coll.last_error_module);
    TEST_ASSERT_EQUAL(1, s_coll.last_error_code);

    nav_health_record_error(&s_coll, NAV_ERR_MODULE_NTRIP, 5);
    TEST_ASSERT_EQUAL(2, s_coll.total_errors);
    TEST_ASSERT_EQUAL(NAV_ERR_MODULE_NTRIP, s_coll.last_error_module);
    TEST_ASSERT_EQUAL(5, s_coll.last_error_code);

    /* Verify it propagates to snapshot */
    nav_health_collect(&s_coll, &s_snap, 1000);
    TEST_ASSERT_EQUAL(2, s_snap.total_errors);
    TEST_ASSERT_EQUAL(NAV_ERR_MODULE_NTRIP, s_snap.last_error_module);
    TEST_ASSERT_EQUAL(5, s_snap.last_error_code);
    TEST_ASSERT_EQUAL(1000, (int)s_snap.uptime_ms);
}

/* Test 14: Full snapshot with all subsystems */
void test_health_full_snapshot(void)
{
    /* Init all subsystems minimally */
    transport_uart_config_t uart_cfg = { .port = BOARD_UART_PORT_PRIMARY, .baudrate = 115200 };
    transport_uart_init(&s_uart_primary, &uart_cfg);
    s_uart_primary.stats.rx_bytes_in = 100;

    gnss_um980_init(&s_gnss_primary, NULL, BOARD_UART_PORT_PRIMARY);
    s_gnss_primary.snapshot.valid = true;
    s_gnss_primary.snapshot.fresh = true;

    gnss_dual_heading_init(&s_heading);
    s_heading.result.valid = true;
    s_heading.calc_count = 10;

    ntrip_client_init(&s_ntrip);
    s_ntrip.state = NTRIP_STATE_CONNECTED;
    s_ntrip.reconnect_count = 0;

    rtcm_router_init(&s_rtcm);
    s_aog.output_state = AOG_OUTPUT_OK;
    s_aog.pgn214_send_count = 100;

    transport_tcp_config_t tcp_cfg = { .remote_ip = 0, .remote_port = 2101 };
    transport_tcp_init(&s_tcp, &tcp_cfg);
    s_tcp.connected = true;

    /* Wire everything */
    nav_health_collector_init(&s_coll);
    nav_health_collector_set_uart_primary(&s_coll, &s_uart_primary);
    nav_health_collector_set_gnss_primary(&s_coll, &s_gnss_primary);
    nav_health_collector_set_heading(&s_coll, &s_heading);
    nav_health_collector_set_ntrip(&s_coll, &s_ntrip);
    nav_health_collector_set_rtcm_router(&s_coll, &s_rtcm);
    nav_health_collector_set_aog_nav_app(&s_coll, &s_aog);
    nav_health_collector_set_tcp(&s_coll, &s_tcp);
    nav_health_collector_set_eth_link(&s_coll, true);

    nav_health_collect(&s_coll, &s_snap, 60000);

    /* Verify all subsystems populated */
    TEST_ASSERT_EQUAL(100, s_snap.uart_primary_rx_bytes);
    TEST_ASSERT_TRUE(s_snap.gnss_primary_valid);
    TEST_ASSERT_TRUE(s_snap.gnss_primary_fresh);
    TEST_ASSERT_TRUE(s_snap.heading_valid);
    TEST_ASSERT_EQUAL(10, s_snap.heading_calc_count);
    TEST_ASSERT_EQUAL(NAV_NTRIP_STATE_CONNECTED, s_snap.ntrip_state);
    TEST_ASSERT_EQUAL(NAV_AOG_STATE_OK, s_snap.aog_output_state);
    TEST_ASSERT_EQUAL(100, s_snap.aog_tx_frames);
    TEST_ASSERT_TRUE(s_snap.tcp_connected);
    TEST_ASSERT_TRUE(s_snap.eth_link_up);
    TEST_ASSERT_EQUAL(60000, (int)s_snap.uptime_ms);
}

/* ====================================================================
 * RATE-LIMITED LOGGING TESTS (15-23)
 * ==================================================================== */

static nav_diag_log_entry_t s_log_entry;

/* Test 15: Log entry init with default interval */
void test_log_entry_init_default(void)
{
    nav_diag_log_entry_init(&s_log_entry, NAV_DIAG_LEVEL_INFO);
    TEST_ASSERT_EQUAL(2000, s_log_entry.interval_ms);
    TEST_ASSERT_EQUAL(0, s_log_entry.last_emit_ms);
    TEST_ASSERT_EQUAL(0, s_log_entry.suppressed_count);
    TEST_ASSERT_EQUAL(0, s_log_entry.total_count);
    TEST_ASSERT_FALSE(s_log_entry.has_pending);
}

/* Test 16: Log entry init with custom interval */
void test_log_entry_init_custom(void)
{
    nav_diag_log_entry_init_ex(&s_log_entry, 10000);
    TEST_ASSERT_EQUAL(10000, s_log_entry.interval_ms);
}

/* Test 17: Log emit — first message always passes */
void test_log_emit_first_passes(void)
{
    nav_diag_log_entry_init(&s_log_entry, NAV_DIAG_LEVEL_INFO);
    bool emitted = nav_diag_log_emit(&s_log_entry, NAV_DIAG_LEVEL_INFO,
                                      "TEST", 0, "first message");
    TEST_ASSERT_TRUE(emitted);
    TEST_ASSERT_EQUAL(1, s_log_entry.total_count);
}

/* Test 18: Log emit rate-limited */
void test_log_emit_rate_limited(void)
{
    nav_diag_log_entry_init_ex(&s_log_entry, 5000); /* 5 second interval */

    /* First at t=0 — passes */
    bool e1 = nav_diag_log_emit(&s_log_entry, NAV_DIAG_LEVEL_INFO,
                                 "TEST", 0, "msg1");
    TEST_ASSERT_TRUE(e1);

    /* Second at t=1000 — suppressed (within 5000ms) */
    bool e2 = nav_diag_log_emit(&s_log_entry, NAV_DIAG_LEVEL_INFO,
                                 "TEST", 1000, "msg2");
    TEST_ASSERT_FALSE(e2);
    TEST_ASSERT_EQUAL(1, s_log_entry.suppressed_count);
    TEST_ASSERT_TRUE(s_log_entry.has_pending);
    TEST_ASSERT_EQUAL(2, s_log_entry.total_count);
}

/* Test 19: Log emit after interval — passes */
void test_log_emit_after_interval(void)
{
    nav_diag_log_entry_init_ex(&s_log_entry, 1000);

    nav_diag_log_emit(&s_log_entry, NAV_DIAG_LEVEL_INFO, "TEST", 0, "msg1");
    nav_diag_log_emit(&s_log_entry, NAV_DIAG_LEVEL_INFO, "TEST", 500, "msg2");
    TEST_ASSERT_FALSE(nav_diag_log_emit(&s_log_entry, NAV_DIAG_LEVEL_INFO,
                                         "TEST", 500, "msg3"));

    /* After interval (1001ms) — should pass and show suppressed count */
    bool e4 = nav_diag_log_emit(&s_log_entry, NAV_DIAG_LEVEL_INFO,
                                 "TEST", 1001, "msg4");
    TEST_ASSERT_TRUE(e4);
    TEST_ASSERT_EQUAL(0, s_log_entry.suppressed_count);
    TEST_ASSERT_FALSE(s_log_entry.has_pending);
    TEST_ASSERT_EQUAL(4, s_log_entry.total_count);
}

/* Test 20: Log emit suppressed count tracking */
void test_log_emit_suppressed_tracking(void)
{
    nav_diag_log_entry_init_ex(&s_log_entry, 10000);

    nav_diag_log_emit(&s_log_entry, NAV_DIAG_LEVEL_WARN, "TEST", 0, "msg");
    /* Suppress 5 messages */
    for (int i = 1; i <= 5; i++) {
        nav_diag_log_emit(&s_log_entry, NAV_DIAG_LEVEL_WARN, "TEST",
                           (uint64_t)(i * 100), "suppressed");
    }
    TEST_ASSERT_EQUAL(5, s_log_entry.suppressed_count);
    TEST_ASSERT_TRUE(s_log_entry.has_pending);
}

/* Test 21: Log stats */
void test_log_stats(void)
{
    nav_diag_log_stats_reset();

    const nav_diag_log_stats_t* stats = nav_diag_log_get_stats();
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_EQUAL(0, stats->total_emitted);
    TEST_ASSERT_EQUAL(0, stats->total_suppressed);
    TEST_ASSERT_EQUAL(0, stats->total_errors);

    nav_diag_log_entry_init_ex(&s_log_entry, 1000);

    /* Emit 2 (1 suppressed) */
    nav_diag_log_emit(&s_log_entry, NAV_DIAG_LEVEL_INFO, "TEST", 0, "msg");
    nav_diag_log_emit(&s_log_entry, NAV_DIAG_LEVEL_INFO, "TEST", 500, "msg");

    /* Emit 1 error */
    nav_diag_log_entry_t err_entry;
    nav_diag_log_entry_init_ex(&err_entry, 100);
    nav_diag_log_emit(&err_entry, NAV_DIAG_LEVEL_ERROR, "TEST", 0, "err");

    stats = nav_diag_log_get_stats();
    TEST_ASSERT_EQUAL(2, stats->total_emitted);
    TEST_ASSERT_EQUAL(1, stats->total_suppressed);
    TEST_ASSERT_EQUAL(1, stats->total_errors);

    nav_diag_log_stats_reset();
    stats = nav_diag_log_get_stats();
    TEST_ASSERT_EQUAL(0, stats->total_emitted);
}

/* Test 22: Log level names */
void test_log_level_names(void)
{
    TEST_ASSERT_EQUAL_STRING("DBG", nav_diag_level_name(NAV_DIAG_LEVEL_DEBUG));
    TEST_ASSERT_EQUAL_STRING("INF", nav_diag_level_name(NAV_DIAG_LEVEL_INFO));
    TEST_ASSERT_EQUAL_STRING("WRN", nav_diag_level_name(NAV_DIAG_LEVEL_WARN));
    TEST_ASSERT_EQUAL_STRING("ERR", nav_diag_level_name(NAV_DIAG_LEVEL_ERROR));
}

/* Test 23: Error counter increment */
void test_error_counter_increment(void)
{
    uint32_t counter = 0;
    uint32_t result = nav_diag_error_increment(&counter, "TEST", "desc");
    TEST_ASSERT_EQUAL(1, counter);
    TEST_ASSERT_EQUAL(1, result);
    result = nav_diag_error_increment(&counter, "TEST", "desc2");
    TEST_ASSERT_EQUAL(2, counter);
    TEST_ASSERT_EQUAL(2, result);
}

/* ====================================================================
 * RECOVERY EVALUATION TESTS (24-31)
 * ==================================================================== */

static nav_health_snapshot_t s_health;
static nav_recovery_status_t s_recovery;

/* Test 24: All healthy → no actions */
void test_recovery_all_healthy(void)
{
    memset(&s_health, 0, sizeof(s_health));
    s_health.tcp_connected = true;
    s_health.eth_link_up = true;
    s_health.gnss_primary_fresh = true;
    s_health.gnss_secondary_fresh = true;
    s_health.ntrip_state = NAV_NTRIP_STATE_CONNECTED;

    nav_recovery_evaluate(&s_health, &s_recovery);
    TEST_ASSERT_EQUAL(NAV_RECOVERY_NONE, s_recovery.actions);
    TEST_ASSERT_FALSE(nav_recovery_needs_action(&s_recovery));
}

/* Test 25: NTRIP disconnect → reconnect */
void test_recovery_ntrip_disconnect(void)
{
    memset(&s_health, 0, sizeof(s_health));
    s_health.ntrip_state = NAV_NTRIP_STATE_ERROR;
    s_health.ntrip_reconnect_count = 2;

    nav_recovery_evaluate(&s_health, &s_recovery);
    TEST_ASSERT_TRUE(s_recovery.ntrip_should_reconnect);
    TEST_ASSERT_EQUAL(2, s_recovery.ntrip_consecutive_errors);
    TEST_ASSERT_TRUE(s_recovery.actions & NAV_RECOVERY_NTRIP_RECONNECT);
    TEST_ASSERT_TRUE(nav_recovery_needs_action(&s_recovery));
}

/* Test 26: TCP disconnect → reconnect */
void test_recovery_tcp_disconnect(void)
{
    memset(&s_health, 0, sizeof(s_health));
    s_health.tcp_connected = false;

    nav_recovery_evaluate(&s_health, &s_recovery);
    TEST_ASSERT_TRUE(s_recovery.tcp_disconnected);
    TEST_ASSERT_TRUE(s_recovery.actions & NAV_RECOVERY_TCP_RECONNECT);
}

/* Test 27: ETH link down → reinit */
void test_recovery_eth_down(void)
{
    memset(&s_health, 0, sizeof(s_health));
    s_health.eth_link_up = false;

    nav_recovery_evaluate(&s_health, &s_recovery);
    TEST_ASSERT_TRUE(s_recovery.eth_link_down);
    TEST_ASSERT_TRUE(s_recovery.actions & NAV_RECOVERY_ETH_REINIT);
}

/* Test 28: GNSS stale → reset flag */
void test_recovery_gnss_stale(void)
{
    memset(&s_health, 0, sizeof(s_health));
    s_health.gnss_primary_fresh = false;
    s_health.gnss_primary_bytes = 5000;  /* was working before */

    nav_recovery_evaluate(&s_health, &s_recovery);
    TEST_ASSERT_TRUE(s_recovery.gnss_primary_stale);
    TEST_ASSERT_TRUE(s_recovery.actions & NAV_RECOVERY_GNSS_RESET);
}

/* Test 29: UART errors → check flag */
void test_recovery_uart_errors(void)
{
    memset(&s_health, 0, sizeof(s_health));
    s_health.uart_primary_rx_overflows = 5;

    nav_recovery_evaluate(&s_health, &s_recovery);
    TEST_ASSERT_TRUE(s_recovery.uart_primary_errors);
    TEST_ASSERT_TRUE(s_recovery.actions & NAV_RECOVERY_UART_CHECK);
}

/* Test 30: Multiple simultaneous issues → combined flags */
void test_recovery_multiple_issues(void)
{
    memset(&s_health, 0, sizeof(s_health));
    s_health.ntrip_state = NAV_NTRIP_STATE_RETRY_WAIT;
    s_health.tcp_connected = false;
    s_health.eth_link_up = false;
    s_health.gnss_primary_fresh = false;
    s_health.gnss_primary_bytes = 1000;
    s_health.uart_primary_rx_overflows = 1;

    nav_recovery_evaluate(&s_health, &s_recovery);
    TEST_ASSERT_TRUE(s_recovery.actions & NAV_RECOVERY_NTRIP_RECONNECT);
    TEST_ASSERT_TRUE(s_recovery.actions & NAV_RECOVERY_TCP_RECONNECT);
    TEST_ASSERT_TRUE(s_recovery.actions & NAV_RECOVERY_ETH_REINIT);
    TEST_ASSERT_TRUE(s_recovery.actions & NAV_RECOVERY_GNSS_RESET);
    TEST_ASSERT_TRUE(s_recovery.actions & NAV_RECOVERY_UART_CHECK);
}

/* Test 31: Recovery needs action */
void test_recovery_needs_action(void)
{
    nav_recovery_status_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.actions = NAV_RECOVERY_NONE;
    TEST_ASSERT_FALSE(nav_recovery_needs_action(&rec));

    rec.actions = NAV_RECOVERY_NTRIP_RECONNECT;
    TEST_ASSERT_TRUE(nav_recovery_needs_action(&rec));

    rec.actions = NAV_RECOVERY_NONE;
    TEST_ASSERT_FALSE(nav_recovery_needs_action(&rec));
}

/* ====================================================================
 * STATE NAME HELPER TESTS (32-35)
 * ==================================================================== */

/* Test 32: NTRIP state names */
void test_ntrip_state_names(void)
{
    TEST_ASSERT_EQUAL_STRING("idle", nav_ntrip_state_name(NAV_NTRIP_STATE_IDLE));
    TEST_ASSERT_EQUAL_STRING("connecting", nav_ntrip_state_name(NAV_NTRIP_STATE_CONNECTING));
    TEST_ASSERT_EQUAL_STRING("connected", nav_ntrip_state_name(NAV_NTRIP_STATE_CONNECTED));
    TEST_ASSERT_EQUAL_STRING("error", nav_ntrip_state_name(NAV_NTRIP_STATE_ERROR));
    TEST_ASSERT_EQUAL_STRING("retry_wait", nav_ntrip_state_name(NAV_NTRIP_STATE_RETRY_WAIT));
    TEST_ASSERT_EQUAL_STRING("unknown", nav_ntrip_state_name(NAV_NTRIP_STATE_UNKNOWN));
}

/* Test 33: AOG state names */
void test_aog_state_names(void)
{
    TEST_ASSERT_EQUAL_STRING("init", nav_aog_state_name(NAV_AOG_STATE_INIT));
    TEST_ASSERT_EQUAL_STRING("ok", nav_aog_state_name(NAV_AOG_STATE_OK));
    TEST_ASSERT_EQUAL_STRING("gnss_invalid", nav_aog_state_name(NAV_AOG_STATE_GNSS_INVALID));
    TEST_ASSERT_EQUAL_STRING("gnss_stale", nav_aog_state_name(NAV_AOG_STATE_GNSS_STALE));
    TEST_ASSERT_EQUAL_STRING("heading_invalid", nav_aog_state_name(NAV_AOG_STATE_HEADING_INVALID));
    TEST_ASSERT_EQUAL_STRING("heading_stale", nav_aog_state_name(NAV_AOG_STATE_HEADING_STALE));
    TEST_ASSERT_EQUAL_STRING("heading_lost", nav_aog_state_name(NAV_AOG_STATE_HEADING_LOST));
    TEST_ASSERT_EQUAL_STRING("suppressed", nav_aog_state_name(NAV_AOG_STATE_SUPPRESSED));
    TEST_ASSERT_EQUAL_STRING("unknown", nav_aog_state_name(NAV_AOG_STATE_UNKNOWN));
}

/* Test 34: Error module names */
void test_error_module_names(void)
{
    TEST_ASSERT_EQUAL_STRING("none", nav_error_module_name(NAV_ERR_MODULE_NONE));
    TEST_ASSERT_EQUAL_STRING("gnss_primary", nav_error_module_name(NAV_ERR_MODULE_GNSS_PRIMARY));
    TEST_ASSERT_EQUAL_STRING("gnss_secondary", nav_error_module_name(NAV_ERR_MODULE_GNSS_SECONDARY));
    TEST_ASSERT_EQUAL_STRING("ntrip", nav_error_module_name(NAV_ERR_MODULE_NTRIP));
    TEST_ASSERT_EQUAL_STRING("tcp", nav_error_module_name(NAV_ERR_MODULE_TCP));
    TEST_ASSERT_EQUAL_STRING("eth", nav_error_module_name(NAV_ERR_MODULE_ETH));
    TEST_ASSERT_EQUAL_STRING("uart_primary", nav_error_module_name(NAV_ERR_MODULE_UART_PRIMARY));
    TEST_ASSERT_EQUAL_STRING("uart_secondary", nav_error_module_name(NAV_ERR_MODULE_UART_SECONDARY));
    TEST_ASSERT_EQUAL_STRING("rtcm", nav_error_module_name(NAV_ERR_MODULE_RTCM));
    TEST_ASSERT_EQUAL_STRING("aog", nav_error_module_name(NAV_ERR_MODULE_AOG));
    TEST_ASSERT_EQUAL_STRING("heading", nav_error_module_name(NAV_ERR_MODULE_HEADING));
}

/* Test 35: Recovery flag names */
void test_recovery_flag_names(void)
{
    TEST_ASSERT_EQUAL_STRING("ntrip_reconnect", nav_recovery_flag_name(NAV_RECOVERY_NTRIP_RECONNECT));
    TEST_ASSERT_EQUAL_STRING("tcp_reconnect", nav_recovery_flag_name(NAV_RECOVERY_TCP_RECONNECT));
    TEST_ASSERT_EQUAL_STRING("eth_reinit", nav_recovery_flag_name(NAV_RECOVERY_ETH_REINIT));
    TEST_ASSERT_EQUAL_STRING("gnss_reset", nav_recovery_flag_name(NAV_RECOVERY_GNSS_RESET));
    TEST_ASSERT_EQUAL_STRING("uart_check", nav_recovery_flag_name(NAV_RECOVERY_UART_CHECK));
    TEST_ASSERT_EQUAL_STRING("none", nav_recovery_flag_name(NAV_RECOVERY_NONE));
}

/* ====================================================================
 * TEST RUNNER
 * ==================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Health Snapshot (Teil 1) */
    RUN_TEST(test_health_init_collector);
    RUN_TEST(test_health_collect_null_collector);
    RUN_TEST(test_health_collect_null_subsystems);
    RUN_TEST(test_health_collect_uart_primary);
    RUN_TEST(test_health_collect_gnss_primary);
    RUN_TEST(test_health_collect_gnss_secondary);
    RUN_TEST(test_health_collect_heading);
    RUN_TEST(test_health_collect_ntrip);
    RUN_TEST(test_health_collect_rtcm_router);
    RUN_TEST(test_health_collect_aog_nav_app);
    RUN_TEST(test_health_collect_tcp);
    RUN_TEST(test_health_collect_eth_link);
    RUN_TEST(test_health_error_recording);
    RUN_TEST(test_health_full_snapshot);

    /* Rate-Limited Logging (Teil 2) */
    RUN_TEST(test_log_entry_init_default);
    RUN_TEST(test_log_entry_init_custom);
    RUN_TEST(test_log_emit_first_passes);
    RUN_TEST(test_log_emit_rate_limited);
    RUN_TEST(test_log_emit_after_interval);
    RUN_TEST(test_log_emit_suppressed_tracking);
    RUN_TEST(test_log_stats);
    RUN_TEST(test_log_level_names);
    RUN_TEST(test_error_counter_increment);

    /* Recovery Evaluation (Teil 3) */
    RUN_TEST(test_recovery_all_healthy);
    RUN_TEST(test_recovery_ntrip_disconnect);
    RUN_TEST(test_recovery_tcp_disconnect);
    RUN_TEST(test_recovery_eth_down);
    RUN_TEST(test_recovery_gnss_stale);
    RUN_TEST(test_recovery_uart_errors);
    RUN_TEST(test_recovery_multiple_issues);
    RUN_TEST(test_recovery_needs_action);

    /* State Name Helpers */
    RUN_TEST(test_ntrip_state_names);
    RUN_TEST(test_aog_state_names);
    RUN_TEST(test_error_module_names);
    RUN_TEST(test_recovery_flag_names);

    return UNITY_END();
}
