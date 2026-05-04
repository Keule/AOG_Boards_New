/* ========================================================================
 * test_board_profile_smoke — Compile-smoke tests for board_profile.c
 *
 * Validates the productive board_profile against relevant #define
 * combinations by using the board_profile_mock.c which mimics the
 * productive ESP32 profile.
 *
 * Expected for current NAV-BOARD-PINS-001 state:
 *   - GNSS_PRIMARY: TX=GPIO2, RX=GPIO4 (Keule reference)
 *   - GNSS_SECONDARY: TX=GPIO33, RX=GPIO34 (GPIO35 removed, unstable)
 *   - Both have UART TX capability (has_uart_tx == true)
 *   - ETH RMII pins: MDC=23, MDIO=18, POWER=12
 *   - SD: disabled (GPIO34 reassigned to GNSS2 RX)
 *   - Misc: SAFETY_IN=15, LOG_SWITCH=0
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

/* ---- NAV-BOARD-PINS-001: Exact pin value verification ---- */

void test_gnss_primary_pins_match_keule_reference(void)
{
    board_uart_pins_t pins;
    bool ok = board_profile_get_uart_pins(BOARD_UART_GNSS_PRIMARY, &pins);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(BOARD_GNSS_UART1_TX_PIN, pins.tx_pin);  /* 2 */
    TEST_ASSERT_EQUAL(BOARD_GNSS_UART1_RX_PIN, pins.rx_pin);  /* 4 */
}

void test_gnss_secondary_pins_match_keule_reference(void)
{
    board_uart_pins_t pins;
    bool ok = board_profile_get_uart_pins(BOARD_UART_GNSS_SECONDARY, &pins);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(BOARD_GNSS_UART2_TX_PIN, pins.tx_pin);  /* 33 */
    TEST_ASSERT_EQUAL(BOARD_GNSS_UART2_RX_PIN, pins.rx_pin);  /* 34 */
}

void test_gnss_tx_pins_not_input_only(void)
{
    /* Classic ESP32: GPIO 34..39 are input-only — must NOT be TX */
    board_uart_pins_t pins;
    board_profile_get_uart_pins(BOARD_UART_GNSS_PRIMARY, &pins);
    TEST_ASSERT_TRUE(pins.tx_pin < 34 || pins.tx_pin > 39);

    board_profile_get_uart_pins(BOARD_UART_GNSS_SECONDARY, &pins);
    TEST_ASSERT_TRUE(pins.tx_pin < 34 || pins.tx_pin > 39);
}

void test_gpio35_not_used_as_tx(void)
{
    /* Hard rule: GPIO35 must NOT be TX on any UART */
    board_uart_pins_t pins;
    board_profile_get_uart_pins(BOARD_UART_GNSS_PRIMARY, &pins);
    TEST_ASSERT_NOT_EQUAL(35, pins.tx_pin);

    board_profile_get_uart_pins(BOARD_UART_GNSS_SECONDARY, &pins);
    TEST_ASSERT_NOT_EQUAL(35, pins.tx_pin);
}

/* ---- ETH RMII pin verification ---- */

void test_eth_rmii_pins_available(void)
{
    board_eth_pins_t pins;
    bool ok = board_profile_get_eth_pins(&pins);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(BOARD_ETH_MDC_PIN, pins.mdc_pin);
    TEST_ASSERT_EQUAL(BOARD_ETH_MDIO_PIN, pins.mdio_pin);
    TEST_ASSERT_EQUAL(BOARD_ETH_POWER_PIN, pins.power_pin);
    TEST_ASSERT_EQUAL(BOARD_ETH_RESET_PIN, pins.reset_pin);
}

/* ---- SD pin verification ----
 * SD is disabled: GPIO34 was reassigned to GNSS2 RX (Task 021-a).
 * board_profile_get_sd_pins() returns false with all pins set to -1. */

void test_sd_pins_disabled(void)
{
    board_sd_pins_t pins;
    bool ok = board_profile_get_sd_pins(&pins);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL(-1, pins.miso_pin);
    TEST_ASSERT_EQUAL(-1, pins.mosi_pin);
    TEST_ASSERT_EQUAL(-1, pins.sclk_pin);
    TEST_ASSERT_EQUAL(-1, pins.cs_pin);
}

/* ---- Misc pin verification ---- */

void test_misc_pins_available(void)
{
    board_misc_pins_t pins;
    bool ok = board_profile_get_misc_pins(&pins);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(BOARD_SAFETY_IN_PIN, pins.safety_in_pin);
    TEST_ASSERT_EQUAL(BOARD_LOG_SWITCH_PIN, pins.log_switch_pin);
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

/* ---- Null pointer safety ---- */

void test_eth_pins_null_returns_false(void)
{
    TEST_ASSERT_FALSE(board_profile_get_eth_pins(NULL));
}

void test_sd_pins_null_returns_false(void)
{
    TEST_ASSERT_FALSE(board_profile_get_sd_pins(NULL));
}

void test_misc_pins_null_returns_false(void)
{
    TEST_ASSERT_FALSE(board_profile_get_misc_pins(NULL));
}

/* ---- Baudrate define ---- */

void test_gnss_baudrate_is_921600(void)
{
    TEST_ASSERT_EQUAL(921600, BOARD_GNSS_UART_BAUDRATE);
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

    /* NAV-BOARD-PINS-001: Exact pin values */
    RUN_TEST(test_gnss_primary_pins_match_keule_reference);
    RUN_TEST(test_gnss_secondary_pins_match_keule_reference);
    RUN_TEST(test_gnss_tx_pins_not_input_only);
    RUN_TEST(test_gpio35_not_used_as_tx);

    /* ETH RMII */
    RUN_TEST(test_eth_rmii_pins_available);

    /* SD Card (disabled) */
    RUN_TEST(test_sd_pins_disabled);

    /* Misc */
    RUN_TEST(test_misc_pins_available);

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

    /* Null pointer safety */
    RUN_TEST(test_eth_pins_null_returns_false);
    RUN_TEST(test_sd_pins_null_returns_false);
    RUN_TEST(test_misc_pins_null_returns_false);

    /* Baudrate */
    RUN_TEST(test_gnss_baudrate_is_921600);

    return UNITY_END();
}
