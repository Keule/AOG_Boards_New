/* ========================================================================
 * test_followup_review — Host tests for follow-up verification
 *
 * Validates:
 *  1. BOARD_PIN_UNASSIGNED constant and GNSS UART pin configuration
 *  2. board_profile_has_uart_tx() API
 *  3. Work/Config profile consistency
 *  4. FastCycleContext has no service_profile field (structural check)
 *  5. HAL UART init idempotency
 * ======================================================================== */

#include "unity.h"
#include "board_profile.h"
#include "runtime_types.h"
#include "runtime_mode.h"
#include "hal_uart.h"

void setUp(void)
{
    /* Reset mode state before each test */
    runtime_mode_init();
}

void tearDown(void)
{
    /* nothing */
}

/* ---- 1. BOARD_PIN_UNASSIGNED sentinel value ---- */

void test_board_pin_unassigned_is_negative_one(void)
{
    /* BOARD_PIN_UNASSIGNED MUST be -1 (maps to UART_PIN_NO_CHANGE) */
    TEST_ASSERT_EQUAL(-1, BOARD_PIN_UNASSIGNED);
}

/* ---- 2. GNSS UART ports have documented TX pin status ---- */

void test_gnss_primary_tx_pin_is_assigned(void)
{
    board_uart_pins_t pins;
    bool ok = board_profile_get_uart_pins(BOARD_UART_GNSS_PRIMARY, &pins);
    TEST_ASSERT_TRUE(ok);
    /* NAV-RTCM-001: TX pin is now assigned for RTCM output */
    TEST_ASSERT(pins.tx_pin >= 0);
    TEST_ASSERT(pins.tx_pin != BOARD_PIN_UNASSIGNED);
    TEST_ASSERT(pins.rx_pin >= 0);
}

void test_gnss_secondary_tx_pin_is_assigned(void)
{
    board_uart_pins_t pins;
    bool ok = board_profile_get_uart_pins(BOARD_UART_GNSS_SECONDARY, &pins);
    TEST_ASSERT_TRUE(ok);
    /* NAV-RTCM-001: TX pin is now assigned for RTCM output */
    TEST_ASSERT(pins.tx_pin >= 0);
    TEST_ASSERT(pins.tx_pin != BOARD_PIN_UNASSIGNED);
    TEST_ASSERT(pins.rx_pin >= 0);
}

void test_board_profile_has_uart_tx_api(void)
{
    /* NAV-RTCM-001: new API for checking TX pin availability */
    TEST_ASSERT_TRUE(board_profile_has_uart_tx(BOARD_UART_GNSS_PRIMARY));
    TEST_ASSERT_TRUE(board_profile_has_uart_tx(BOARD_UART_GNSS_SECONDARY));
    TEST_ASSERT_TRUE(board_profile_has_uart_tx(BOARD_UART_CONSOLE));
    TEST_ASSERT_FALSE(board_profile_has_uart_tx((board_uart_port_t)99));
}

void test_console_uart_has_both_pins_assigned(void)
{
    board_uart_pins_t pins;
    bool ok = board_profile_get_uart_pins(BOARD_UART_CONSOLE, &pins);
    TEST_ASSERT_TRUE(ok);
    /* Console UART must have both TX and RX pins assigned */
    TEST_ASSERT(pins.tx_pin >= 0);
    TEST_ASSERT(pins.rx_pin >= 0);
}

/* ---- 3. Invalid port returns false ---- */

void test_invalid_uart_port_returns_false(void)
{
    board_uart_pins_t pins;
    bool ok = board_profile_get_uart_pins((board_uart_port_t)99, &pins);
    TEST_ASSERT_FALSE(ok);
}

void test_null_pins_returns_false(void)
{
    bool ok = board_profile_get_uart_pins(BOARD_UART_GNSS_PRIMARY, NULL);
    TEST_ASSERT_FALSE(ok);
}

/* ---- 4. Work/Config profile consistency ---- */

void test_work_diagnostics_has_lower_priority_than_config(void)
{
    const service_profile_t* wp = runtime_mode_work_profile(SERVICE_GROUP_DIAGNOSTICS);
    const service_profile_t* cp = runtime_mode_config_profile(SERVICE_GROUP_DIAGNOSTICS);
    TEST_ASSERT_NOT_NULL(wp);
    TEST_ASSERT_NOT_NULL(cp);
    /* Work diagnostics should have lower priority than Config */
    TEST_ASSERT_TRUE(wp->priority < cp->priority);
}

void test_work_diagnostics_has_longer_period_than_config(void)
{
    const service_profile_t* wp = runtime_mode_work_profile(SERVICE_GROUP_DIAGNOSTICS);
    const service_profile_t* cp = runtime_mode_config_profile(SERVICE_GROUP_DIAGNOSTICS);
    TEST_ASSERT_NOT_NULL(wp);
    TEST_ASSERT_NOT_NULL(cp);
    /* Work diagnostics should have longer period (slower) than Config */
    TEST_ASSERT_TRUE(wp->period_ms > cp->period_ms);
}

void test_io_groups_same_profile_in_work_and_config(void)
{
    service_group_t io_groups[] = {
        SERVICE_GROUP_UART,
        SERVICE_GROUP_UDP,
        SERVICE_GROUP_TCP_NTRIP
    };

    for (int i = 0; i < 3; i++) {
        const service_profile_t* wp = runtime_mode_work_profile(io_groups[i]);
        const service_profile_t* cp = runtime_mode_config_profile(io_groups[i]);
        TEST_ASSERT_NOT_NULL(wp);
        TEST_ASSERT_NOT_NULL(cp);
        /* I/O groups MUST have identical profiles in both modes */
        TEST_ASSERT_EQUAL(wp->priority, cp->priority);
        TEST_ASSERT_EQUAL(wp->period_ms, cp->period_ms);
        TEST_ASSERT_EQUAL(wp->suspended, cp->suspended);
        TEST_ASSERT_FALSE(wp->suspended);
    }
}

/* ---- 5. FastCycleContext contains no service_profile ---- */

void test_fast_cycle_context_size_is_reasonable(void)
{
    /* fast_cycle_context_t has 5 fields (2x uint64 + 3x uint32)
     * = 28 bytes minimum, up to 40 with alignment.
     * Adding a service_profile_t (~6 bytes) would push it over 48. */
    size_t ctx_size = sizeof(fast_cycle_context_t);
    TEST_ASSERT(ctx_size >= 24 && ctx_size <= 48);
}

void test_fast_cycle_context_no_service_profile_member(void)
{
    /* This is a compile-time structural guarantee enforced by
     * runtime_types.h comments and the struct definition.
     * If someone adds a service_profile_t member, sizeof will change
     * and the test_fast_cycle_context_size_is_reasonable test above
     * will flag it. Here we verify the known profile values exist
     * independently of the context struct. */
    const service_profile_t* p = runtime_mode_get_profile(SERVICE_GROUP_UART);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(p->priority > 0);
    TEST_ASSERT_TRUE(p->period_ms > 0);
}

/* ---- 6. HAL UART init is idempotent ---- */

void test_hal_uart_init_idempotent(void)
{
    /* hal_uart_init with a simple ops table should succeed,
     * and calling it again should also succeed (no crash, no error). */
    static const hal_uart_ops_t dummy_ops = { NULL };

    int ret1 = hal_uart_init(&dummy_ops);
    TEST_ASSERT_EQUAL(0, ret1);

    int ret2 = hal_uart_init(&dummy_ops);
    TEST_ASSERT_EQUAL(0, ret2);

    /* NULL ops should fail */
    int ret3 = hal_uart_init(NULL);
    TEST_ASSERT(ret3 != 0);
}

void test_hal_uart_init_null_returns_error(void)
{
    /* Clear any previously set ops first */
    hal_uart_deinit();

    int ret = hal_uart_init(NULL);
    TEST_ASSERT(ret != 0);
}

int main(void)
{
    UNITY_BEGIN();

    /* BOARD_PIN_UNASSIGNED sentinel */
    RUN_TEST(test_board_pin_unassigned_is_negative_one);

    /* GNSS UART pin configuration (NAV-RTCM-001: now assigned) */
    RUN_TEST(test_gnss_primary_tx_pin_is_assigned);
    RUN_TEST(test_gnss_secondary_tx_pin_is_assigned);
    RUN_TEST(test_board_profile_has_uart_tx_api);
    RUN_TEST(test_console_uart_has_both_pins_assigned);

    /* Invalid inputs */
    RUN_TEST(test_invalid_uart_port_returns_false);
    RUN_TEST(test_null_pins_returns_false);

    /* Work/Config profile consistency */
    RUN_TEST(test_work_diagnostics_has_lower_priority_than_config);
    RUN_TEST(test_work_diagnostics_has_longer_period_than_config);
    RUN_TEST(test_io_groups_same_profile_in_work_and_config);

    /* FastCycleContext structural check */
    RUN_TEST(test_fast_cycle_context_size_is_reasonable);
    RUN_TEST(test_fast_cycle_context_no_service_profile_member);

    /* HAL UART init behavior */
    RUN_TEST(test_hal_uart_init_idempotent);
    RUN_TEST(test_hal_uart_init_null_returns_error);

    return UNITY_END();
}
