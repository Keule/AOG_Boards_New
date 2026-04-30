/* ========================================================================
 * test_board_profile_smoke — Compile-smoke tests for board_profile.c
 *
 * Validates the productive board_profile against relevant #define
 * combinations by using the board_profile_mock.c which mimics the
 * productive ESP32 profile.
 *
 * Expected for current NAV-RTCM-001 state:
 *   - GNSS_PRIMARY active → valid TX pin and RX pin
 *   - GNSS_SECONDARY active → valid TX pin and RX pin
 *   - Both have UART TX capability (has_uart_tx == true)
 *   - GNSS port iteration table contains both
 * ======================================================================== */

#include "unity.h"
#include "board_profile.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ---- ESP32 NAVIGATION Profile (simulated by mock) ---- */

void test_esp32_nav_gnss_primary_has_valid_pins(void)
{
    board_uart_pins_t pins;
    bool ok = board_profile_get_uart_pins(BOARD_UART_GNSS_PRIMARY, &pins);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT(pins.tx_pin != BOARD_PIN_UNASSIGNED);
    TEST_ASSERT(pins.rx_pin != BOARD_PIN_UNASSIGNED);
    TEST_ASSERT(pins.tx_pin >= 0);
    TEST_ASSERT(pins.rx_pin >= 0);
}

void test_esp32_nav_gnss_secondary_has_valid_pins(void)
{
    board_uart_pins_t pins;
    bool ok = board_profile_get_uart_pins(BOARD_UART_GNSS_SECONDARY, &pins);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT(pins.tx_pin != BOARD_PIN_UNASSIGNED);
    TEST_ASSERT(pins.rx_pin != BOARD_PIN_UNASSIGNED);
    TEST_ASSERT(pins.tx_pin >= 0);
    TEST_ASSERT(pins.rx_pin >= 0);
}

void test_esp32_nav_gnss_primary_has_tx(void)
{
    TEST_ASSERT_TRUE(board_profile_has_uart_tx(BOARD_UART_GNSS_PRIMARY));
}

void test_esp32_nav_gnss_secondary_has_tx(void)
{
    TEST_ASSERT_TRUE(board_profile_has_uart_tx(BOARD_UART_GNSS_SECONDARY));
}

void test_esp32_nav_console_has_both_pins(void)
{
    board_uart_pins_t pins;
    bool ok = board_profile_get_uart_pins(BOARD_UART_CONSOLE, &pins);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT(pins.tx_pin >= 0);
    TEST_ASSERT(pins.rx_pin >= 0);
}

/* ---- GNSS Port Iteration ---- */

void test_gnss_port_iteration_table_complete(void)
{
    int count = board_profile_get_gnss_port_count();
    TEST_ASSERT_EQUAL(2, count);  /* Primary + Secondary */

    /* Verify both are in the table */
    bool found_primary = false, found_secondary = false;
    for (int i = 0; i < count; i++) {
        board_uart_port_t port = board_profile_get_gnss_port(i);
        if (port == BOARD_UART_GNSS_PRIMARY)   found_primary = true;
        if (port == BOARD_UART_GNSS_SECONDARY) found_secondary = true;
    }
    TEST_ASSERT_TRUE(found_primary);
    TEST_ASSERT_TRUE(found_secondary);
}

void test_gnss_port_iteration_all_have_tx(void)
{
    int count = board_profile_get_gnss_port_count();
    for (int i = 0; i < count; i++) {
        board_uart_port_t port = board_profile_get_gnss_port(i);
        TEST_ASSERT_TRUE(board_profile_has_uart(port));
        TEST_ASSERT_TRUE(board_profile_has_uart_tx(port));
    }
}

/* ---- BOARD_PIN_UNASSIGNED sentinel ---- */

void test_board_pin_unassigned_is_negative_one(void)
{
    TEST_ASSERT_EQUAL(-1, BOARD_PIN_UNASSIGNED);
}

void test_no_gnss_port_has_unassigned_tx(void)
{
    int count = board_profile_get_gnss_port_count();
    for (int i = 0; i < count; i++) {
        board_uart_port_t port = board_profile_get_gnss_port(i);
        board_uart_pins_t pins;
        bool ok = board_profile_get_uart_pins(port, &pins);
        TEST_ASSERT_TRUE(ok);
        TEST_ASSERT(pins.tx_pin != BOARD_PIN_UNASSIGNED);
    }
}

/* ---- Board type detection ---- */

void test_board_type_is_known(void)
{
    board_type_t board = board_profile_get_board();
    TEST_ASSERT(board != BOARD_UNKNOWN);
}

/* ---- Invalid port handling ---- */

void test_invalid_port_returns_no_pins(void)
{
    board_uart_pins_t pins;
    bool ok = board_profile_get_uart_pins((board_uart_port_t)99, &pins);
    TEST_ASSERT_FALSE(ok);
}

void test_invalid_port_no_uart(void)
{
    TEST_ASSERT_FALSE(board_profile_has_uart((board_uart_port_t)99));
}

void test_invalid_port_no_tx(void)
{
    TEST_ASSERT_FALSE(board_profile_has_uart_tx((board_uart_port_t)99));
}

int main(void)
{
    UNITY_BEGIN();

    /* ESP32 NAVIGATION profile (simulated) */
    RUN_TEST(test_esp32_nav_gnss_primary_has_valid_pins);
    RUN_TEST(test_esp32_nav_gnss_secondary_has_valid_pins);
    RUN_TEST(test_esp32_nav_gnss_primary_has_tx);
    RUN_TEST(test_esp32_nav_gnss_secondary_has_tx);
    RUN_TEST(test_esp32_nav_console_has_both_pins);

    /* GNSS Port Iteration */
    RUN_TEST(test_gnss_port_iteration_table_complete);
    RUN_TEST(test_gnss_port_iteration_all_have_tx);

    /* BOARD_PIN_UNASSIGNED sentinel */
    RUN_TEST(test_board_pin_unassigned_is_negative_one);
    RUN_TEST(test_no_gnss_port_has_unassigned_tx);

    /* Board type */
    RUN_TEST(test_board_type_is_known);

    /* Invalid port handling */
    RUN_TEST(test_invalid_port_returns_no_pins);
    RUN_TEST(test_invalid_port_no_uart);
    RUN_TEST(test_invalid_port_no_tx);

    return UNITY_END();
}
