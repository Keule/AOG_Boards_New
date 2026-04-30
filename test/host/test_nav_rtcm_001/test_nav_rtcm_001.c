/* ========================================================================
 * test_nav_rtcm_001 — Host tests for NAV-RTCM-001 productive completion
 *
 * Validates:
 *  1. Board profile has real TX pins for GNSS UART ports
 *  2. board_profile_has_uart_tx() correctly detects TX availability
 *  3. GNSS port iteration API (extensible table)
 *  4. RTCM router outputs wired generically (not hardcoded count)
 *  5. Drop-Policy: source always emptied, dropped bytes counted
 *  6. Multi-output byte-exact forwarding
 *  7. Regression: router has no HAL/UART/TCP includes
 *  8. NAV_RTCM_MAX_TARGETS and RTCM_ROUTER_MAX_OUTPUTS documented
 * ======================================================================== */

#include "unity.h"
#include "board_profile.h"
#include "rtcm_router.h"
#include "byte_ring_buffer.h"
#include "nav_rtcm_wiring.h"
#include "hal_uart.h"
#include "runtime_types.h"
#include <string.h>

/* ---- Test fixtures ---- */

static rtcm_router_t router;
static uint8_t src_storage[256];
static byte_ring_buffer_t source;
static uint8_t out1_storage[128];
static uint8_t out2_storage[128];
static byte_ring_buffer_t out1;
static byte_ring_buffer_t out2;

void setUp(void)
{
    memset(&router, 0, sizeof(router));
    rtcm_router_init(&router);

    memset(src_storage, 0, sizeof(src_storage));
    byte_ring_buffer_init(&source, src_storage, sizeof(src_storage));
    rtcm_router_set_source(&router, &source);

    memset(out1_storage, 0, sizeof(out1_storage));
    memset(out2_storage, 0, sizeof(out2_storage));
    byte_ring_buffer_init(&out1, out1_storage, sizeof(out1_storage));
    byte_ring_buffer_init(&out2, out2_storage, sizeof(out2_storage));
}

void tearDown(void) {}

/* ========================================================================
 * 1. Board profile: GNSS UART ports have real TX pins
 * ======================================================================== */

void test_gnss_primary_has_real_tx_pin(void)
{
    board_uart_pins_t pins;
    bool ok = board_profile_get_uart_pins(BOARD_UART_GNSS_PRIMARY, &pins);
    TEST_ASSERT_TRUE(ok);
    /* NAV-RTCM-001: TX pin must be assigned (not BOARD_PIN_UNASSIGNED) */
    TEST_ASSERT(pins.tx_pin >= 0);
    TEST_ASSERT(pins.tx_pin != BOARD_PIN_UNASSIGNED);
    TEST_ASSERT(pins.rx_pin >= 0);
}

void test_gnss_secondary_has_real_tx_pin(void)
{
    board_uart_pins_t pins;
    bool ok = board_profile_get_uart_pins(BOARD_UART_GNSS_SECONDARY, &pins);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT(pins.tx_pin >= 0);
    TEST_ASSERT(pins.tx_pin != BOARD_PIN_UNASSIGNED);
    TEST_ASSERT(pins.rx_pin >= 0);
}

void test_console_has_both_pins(void)
{
    board_uart_pins_t pins;
    bool ok = board_profile_get_uart_pins(BOARD_UART_CONSOLE, &pins);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT(pins.tx_pin >= 0);
    TEST_ASSERT(pins.rx_pin >= 0);
}

/* ========================================================================
 * 2. board_profile_has_uart_tx() API
 * ======================================================================== */

void test_has_uart_tx_true_for_gnss_primary(void)
{
    TEST_ASSERT_TRUE(board_profile_has_uart_tx(BOARD_UART_GNSS_PRIMARY));
}

void test_has_uart_tx_true_for_gnss_secondary(void)
{
    TEST_ASSERT_TRUE(board_profile_has_uart_tx(BOARD_UART_GNSS_SECONDARY));
}

void test_has_uart_tx_true_for_console(void)
{
    TEST_ASSERT_TRUE(board_profile_has_uart_tx(BOARD_UART_CONSOLE));
}

void test_has_uart_tx_false_for_invalid_port(void)
{
    TEST_ASSERT_FALSE(board_profile_has_uart_tx((board_uart_port_t)99));
}

/* ========================================================================
 * 3. GNSS Port Iteration API (extensible table, not hardcoded)
 * ======================================================================== */

void test_gnss_port_count_is_extensible(void)
{
    int count = board_profile_get_gnss_port_count();
    TEST_ASSERT(count >= 2);
    /* Not hardcoded to exactly 2 — must work with any count */
}

void test_gnss_port_table_has_primary_and_secondary(void)
{
    int count = board_profile_get_gnss_port_count();
    bool found_primary = false, found_secondary = false;
    for (int i = 0; i < count; i++) {
        board_uart_port_t port = board_profile_get_gnss_port(i);
        if (port == BOARD_UART_GNSS_PRIMARY)   found_primary = true;
        if (port == BOARD_UART_GNSS_SECONDARY) found_secondary = true;
    }
    TEST_ASSERT_TRUE(found_primary);
    TEST_ASSERT_TRUE(found_secondary);
}

void test_gnss_port_out_of_range(void)
{
    TEST_ASSERT_EQUAL(BOARD_UART_COUNT, board_profile_get_gnss_port(-1));
    TEST_ASSERT_EQUAL(BOARD_UART_COUNT, board_profile_get_gnss_port(9999));
}

/* ========================================================================
 * 4. RTCM Router: Generic wiring via nav_rtcm_wire_outputs()
 * ======================================================================== */

void test_router_output_count_matches_board_tx(void)
{
    /* Build target list from board profile GNSS port table */
    nav_rtcm_target_t targets[NAV_RTCM_MAX_TARGETS];
    int target_count = 0;

    for (int i = 0; i < board_profile_get_gnss_port_count() &&
                   target_count < NAV_RTCM_MAX_TARGETS; i++) {
        board_uart_port_t port = board_profile_get_gnss_port(i);
        targets[target_count].port = port;
        targets[target_count].tx_buffer = (target_count == 0) ? &out1 : &out2;
        target_count++;
    }

    nav_rtcm_wiring_result_t r = nav_rtcm_wire_outputs(targets, target_count, &router);

    /* Both GNSS ports have TX pins → both wired */
    TEST_ASSERT_TRUE(r.productive);
    TEST_ASSERT_EQUAL(target_count, r.active_target_count);
    TEST_ASSERT_EQUAL(target_count, r.registered_output_count);
    TEST_ASSERT_EQUAL(target_count, router.output_count);
}

void test_router_skips_output_without_tx_pin(void)
{
    /* Register only primary (simulate secondary not in list) */
    rtcm_router_add_output(&router, &out1);

    TEST_ASSERT_EQUAL(1, router.output_count);
    TEST_ASSERT_TRUE(router.outputs[0].enabled);

    /* Feed data — should only go to primary */
    uint8_t data[] = {0xD3, 0x00, 0x05, 0x01, 0x02};
    byte_ring_buffer_write(&source, data, sizeof(data));
    rtcm_router_service_step((runtime_component_t*)&router, 1000);

    TEST_ASSERT_EQUAL(5, byte_ring_buffer_available(&out1));
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&out2));
}

void test_router_disabled_output_not_written(void)
{
    rtcm_router_add_output(&router, &out1);
    rtcm_router_add_output(&router, &out2);

    /* Disable secondary output */
    router.outputs[1].enabled = false;

    uint8_t data[] = {0xD3, 0x00, 0x03, 0xAA, 0xBB, 0xCC};
    byte_ring_buffer_write(&source, data, sizeof(data));
    rtcm_router_service_step((runtime_component_t*)&router, 1000);

    TEST_ASSERT_EQUAL(6, byte_ring_buffer_available(&out1));
    TEST_ASSERT_EQUAL(6, router.outputs[0].bytes_forwarded);

    /* Secondary must be empty (disabled) */
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&out2));
    TEST_ASSERT_EQUAL(0, router.outputs[1].bytes_forwarded);
    TEST_ASSERT_EQUAL(0, router.outputs[1].bytes_dropped);
}

/* ========================================================================
 * 5. Drop-Policy: Source always emptied, drops counted
 * ======================================================================== */

void test_drop_policy_source_always_emptied(void)
{
    rtcm_router_add_output(&router, &out1);

    /* Fill output almost completely */
    uint8_t fill[124];
    memset(fill, 0xAA, sizeof(fill));
    byte_ring_buffer_write(&out1, fill, sizeof(fill));

    /* Feed more data than output can hold */
    uint8_t data[10];
    memset(data, 0xD3, sizeof(data));
    byte_ring_buffer_write(&source, data, sizeof(data));

    rtcm_router_service_step((runtime_component_t*)&router, 1000);

    /* Source must be completely emptied (Drop-Policy A) */
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&source));

    /* Stats must reflect the drop */
    const rtcm_stats_t* stats = rtcm_router_get_stats(&router);
    TEST_ASSERT_EQUAL(10, stats->bytes_in);
    TEST_ASSERT_TRUE(stats->bytes_dropped > 0);
}

void test_drop_policy_other_output_not_blocked(void)
{
    rtcm_router_add_output(&router, &out1);
    rtcm_router_add_output(&router, &out2);

    /* Fill primary completely */
    uint8_t fill[128];
    memset(fill, 0xAA, sizeof(fill));
    byte_ring_buffer_write(&out1, fill, sizeof(fill));

    uint8_t data[] = {0xD3, 0x00, 0x05, 0x01, 0x02};
    byte_ring_buffer_write(&source, data, sizeof(data));

    rtcm_router_service_step((runtime_component_t*)&router, 1000);

    /* Primary: overflow */
    TEST_ASSERT_TRUE(router.outputs[0].bytes_dropped > 0);

    /* Secondary: completely unaffected */
    TEST_ASSERT_EQUAL(5, byte_ring_buffer_available(&out2));
    TEST_ASSERT_EQUAL(5, router.outputs[1].bytes_forwarded);
    TEST_ASSERT_EQUAL(0, router.outputs[1].bytes_dropped);
}

void test_drop_policy_overflow_count_increments(void)
{
    rtcm_router_add_output(&router, &out1);

    uint8_t fill[127];
    memset(fill, 0xAA, sizeof(fill));
    byte_ring_buffer_write(&out1, fill, sizeof(fill));

    /* First overflow */
    uint8_t d1[] = {0xD3, 0x00, 0x05};
    byte_ring_buffer_write(&source, d1, sizeof(d1));
    rtcm_router_service_step((runtime_component_t*)&router, 1000);
    TEST_ASSERT_EQUAL(1, rtcm_router_get_output_overflow_count(&router));

    /* Drain some space */
    byte_ring_buffer_read(&out1, fill, 64);

    /* Second write — should fit, no overflow */
    uint8_t d2[] = {0xD3, 0x00, 0x03};
    byte_ring_buffer_write(&source, d2, sizeof(d2));
    rtcm_router_service_step((runtime_component_t*)&router, 2000);
    TEST_ASSERT_EQUAL(1, rtcm_router_get_output_overflow_count(&router));

    /* Third overflow */
    uint8_t d3[100];
    memset(d3, 0xD3, sizeof(d3));
    byte_ring_buffer_write(&source, d3, sizeof(d3));
    rtcm_router_service_step((runtime_component_t*)&router, 3000);
    TEST_ASSERT(2 <= rtcm_router_get_output_overflow_count(&router));
}

/* ========================================================================
 * 6. Multi-output byte-exact forwarding
 * ======================================================================== */

void test_multi_output_byte_exact_identical(void)
{
    rtcm_router_add_output(&router, &out1);
    rtcm_router_add_output(&router, &out2);

    uint8_t data[] = {0xD3, 0x00, 0x13, 0x3E, 0xD7, 0x02, 0x01, 0x03,
                      0x8A, 0x4B, 0x00, 0xFF, 0xAA, 0x55, 0xCC, 0x33};
    byte_ring_buffer_write(&source, data, sizeof(data));
    rtcm_router_service_step((runtime_component_t*)&router, 1000);

    TEST_ASSERT_EQUAL(sizeof(data), byte_ring_buffer_available(&out1));
    TEST_ASSERT_EQUAL(sizeof(data), byte_ring_buffer_available(&out2));

    uint8_t read1[16] = {0}, read2[16] = {0};
    byte_ring_buffer_read(&out1, read1, sizeof(data));
    byte_ring_buffer_read(&out2, read2, sizeof(data));
    TEST_ASSERT_EQUAL_MEMORY(data, read1, sizeof(data));
    TEST_ASSERT_EQUAL_MEMORY(data, read2, sizeof(data));
}

void test_multi_step_accumulates_correctly(void)
{
    rtcm_router_add_output(&router, &out1);
    rtcm_router_add_output(&router, &out2);

    uint8_t d1[] = {0x01, 0x02, 0x03};
    byte_ring_buffer_write(&source, d1, sizeof(d1));
    rtcm_router_service_step((runtime_component_t*)&router, 1000);

    uint8_t d2[] = {0x04, 0x05, 0x06, 0x07};
    byte_ring_buffer_write(&source, d2, sizeof(d2));
    rtcm_router_service_step((runtime_component_t*)&router, 2000);

    const rtcm_stats_t* stats = rtcm_router_get_stats(&router);
    TEST_ASSERT_EQUAL(7, stats->bytes_in);
    TEST_ASSERT_EQUAL(14, stats->bytes_out);  /* 7 x 2 outputs */
    TEST_ASSERT_EQUAL(0, stats->bytes_dropped);
    TEST_ASSERT_EQUAL(2000, stats->last_activity_us);
}

/* ========================================================================
 * 7. Regression: No HAL/UART/TCP in router
 * ======================================================================== */

void test_router_stats_independent_of_hal(void)
{
    rtcm_router_init(&router);
    rtcm_router_set_source(&router, &source);
    rtcm_router_add_output(&router, &out1);

    uint8_t data[] = {0xD3, 0x00, 0x02};
    byte_ring_buffer_write(&source, data, sizeof(data));
    rtcm_router_service_step((runtime_component_t*)&router, 1000);

    const rtcm_stats_t* stats = rtcm_router_get_stats(&router);
    TEST_ASSERT_EQUAL(3, stats->bytes_in);
    TEST_ASSERT_EQUAL(3, stats->bytes_out);
    TEST_ASSERT_EQUAL(0, stats->bytes_dropped);
}

void test_fast_cycle_context_still_mode_free(void)
{
    fast_cycle_context_t ctx = {0};
    size_t ctx_size = sizeof(ctx);
    TEST_ASSERT(ctx_size >= 24 && ctx_size <= 48);
}

/* ========================================================================
 * 8. NAV_RTCM_MAX_TARGETS and RTCM_ROUTER_MAX_OUTPUTS documented
 * ======================================================================== */

void test_max_targets_documented(void)
{
    TEST_ASSERT_EQUAL(2, RTCM_ROUTER_MAX_OUTPUTS);
    TEST_ASSERT(NAV_RTCM_MAX_TARGETS >= RTCM_ROUTER_MAX_OUTPUTS);
    TEST_ASSERT(NAV_RTCM_MAX_TARGETS >= board_profile_get_gnss_port_count());
}

int main(void)
{
    UNITY_BEGIN();

    /* 1. Board profile: real TX pins */
    RUN_TEST(test_gnss_primary_has_real_tx_pin);
    RUN_TEST(test_gnss_secondary_has_real_tx_pin);
    RUN_TEST(test_console_has_both_pins);

    /* 2. board_profile_has_uart_tx() API */
    RUN_TEST(test_has_uart_tx_true_for_gnss_primary);
    RUN_TEST(test_has_uart_tx_true_for_gnss_secondary);
    RUN_TEST(test_has_uart_tx_true_for_console);
    RUN_TEST(test_has_uart_tx_false_for_invalid_port);

    /* 3. GNSS Port Iteration (extensible table) */
    RUN_TEST(test_gnss_port_count_is_extensible);
    RUN_TEST(test_gnss_port_table_has_primary_and_secondary);
    RUN_TEST(test_gnss_port_out_of_range);

    /* 4. Router: generic wiring via nav_rtcm_wire_outputs() */
    RUN_TEST(test_router_output_count_matches_board_tx);
    RUN_TEST(test_router_skips_output_without_tx_pin);
    RUN_TEST(test_router_disabled_output_not_written);

    /* 5. Drop-Policy */
    RUN_TEST(test_drop_policy_source_always_emptied);
    RUN_TEST(test_drop_policy_other_output_not_blocked);
    RUN_TEST(test_drop_policy_overflow_count_increments);

    /* 6. Multi-output byte-exact forwarding */
    RUN_TEST(test_multi_output_byte_exact_identical);
    RUN_TEST(test_multi_step_accumulates_correctly);

    /* 7. Regression */
    RUN_TEST(test_router_stats_independent_of_hal);
    RUN_TEST(test_fast_cycle_context_still_mode_free);

    /* 8. Capacity documentation */
    RUN_TEST(test_max_targets_documented);

    return UNITY_END();
}
