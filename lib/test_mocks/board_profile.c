/* Mock board_profile implementation for native/host builds.
 *
 * Provides the same API as components/board_profiles/board_profile.c
 * but without any ESP-IDF dependencies (no esp_log.h, no ESP_LOG macros).
 *
 * This file is compiled ONLY for native tests via extra_scripts/native_test.py
 * and overrides the real board_profile.c.
 *
 * Mock board: LilyGO T-ETH Lite ESP32 with GNSS UART TX pins assigned
 * (matching the productive board_profile.c for NAV-BOARD-PINS-001).
 *
 * Pin matrix matches Keule/ESP32_AGO_GNSS LILYGO_T_ETH_LITE_ESP32_board_pins.h:
 *   GNSS1: TX=GPIO2, RX=GPIO4
 *   GNSS2: TX=GPIO33, RX=GPIO34
 *   ETH:   RTL8201/RMII, MDC=23, MDIO=18, POWER=12
 *   SD:    disabled (GPIO34 reassigned to GNSS2 RX)
 *   MISC:  SAFETY_IN=15, LOG_SWITCH=0 */
#include <stddef.h>
#include "board_profile.h"

board_type_t board_profile_get_board(void)
{
    return BOARD_LILYGO_T_ETH_LITE_ESP32;
}

ethernet_kind_t board_profile_get_eth(void)
{
    return ETH_INTERNAL_MAC_RMII;
}

bool board_profile_has_uart(board_uart_port_t port)
{
    switch (port) {
    case BOARD_UART_CONSOLE:
        return true;
    case BOARD_UART_GNSS_PRIMARY:
    case BOARD_UART_GNSS_SECONDARY:
        return true;   /* All ports available in native test mode */
    default:
        return false;
    }
}

bool board_profile_has_spi(board_spi_bus_t bus)
{
    (void)bus;
    return false;
}

unsigned int board_profile_get_features(void)
{
    return BOARD_FEATURE_ETHERNET | BOARD_FEATURE_UART_GNSS;
}

bool board_profile_get_uart_pins(board_uart_port_t port, board_uart_pins_t* pins)
{
    if (pins == NULL) {
        return false;
    }
    switch (port) {
    case BOARD_UART_CONSOLE:
        pins->tx_pin = 1;
        pins->rx_pin = 3;
        return true;
    case BOARD_UART_GNSS_PRIMARY:
        /* Matching productive board: UART_NUM_1 TX=GPIO2, RX=GPIO4
         * Source: Keule/ESP32_AGO_GNSS LILYGO_T_ETH_LITE_ESP32_board_pins.h */
        pins->tx_pin = BOARD_GNSS_UART1_TX_PIN;  /* 2 */
        pins->rx_pin = BOARD_GNSS_UART1_RX_PIN;  /* 4 */
        return true;
    case BOARD_UART_GNSS_SECONDARY:
        /* Matching productive board: UART_NUM_2 TX=GPIO33, RX=GPIO34
         * NOTE: GPIO35 removed (unstable), GPIO34 was previously SD_MISO */
        pins->tx_pin = BOARD_GNSS_UART2_TX_PIN;  /* 33 */
        pins->rx_pin = BOARD_GNSS_UART2_RX_PIN;  /* 34 */
        return true;
    default:
        pins->tx_pin = -1;
        pins->rx_pin = -1;
        return false;
    }
}

bool board_profile_has_uart_tx(board_uart_port_t port)
{
    if (!board_profile_has_uart(port)) {
        return false;
    }
    board_uart_pins_t pins;
    if (!board_profile_get_uart_pins(port, &pins)) {
        return false;
    }
    return pins.tx_pin != BOARD_PIN_UNASSIGNED;
}

bool board_profile_get_eth_pins(board_eth_pins_t* pins)
{
    if (pins == NULL) {
        return false;
    }
    /* Mock matches productive ESP32 NAV: RTL8201 RMII */
    pins->phy_type   = BOARD_ETH_PHY_TYPE;
    pins->phy_addr   = BOARD_ETH_PHY_ADDR;
    pins->clk_mode   = BOARD_ETH_CLK_MODE;
    pins->reset_pin  = BOARD_ETH_RESET_PIN;
    pins->mdc_pin    = BOARD_ETH_MDC_PIN;
    pins->mdio_pin   = BOARD_ETH_MDIO_PIN;
    pins->power_pin  = BOARD_ETH_POWER_PIN;
    return true;
}

bool board_profile_get_sd_pins(board_sd_pins_t* pins)
{
    if (pins == NULL) {
        return false;
    }
    /* SD disabled: GPIO34 reassigned to GNSS2 RX, SD driver never initialized */
    pins->miso_pin = -1;
    pins->mosi_pin = -1;
    pins->sclk_pin = -1;
    pins->cs_pin   = -1;
    return false;
}

bool board_profile_get_misc_pins(board_misc_pins_t* pins)
{
    if (pins == NULL) {
        return false;
    }
    /* Mock matches productive ESP32 NAV: Safety + Log Switch */
    pins->safety_in_pin  = BOARD_SAFETY_IN_PIN;
    pins->log_switch_pin = BOARD_LOG_SWITCH_PIN;
    return true;
}

/* ---- GNSS Port Iteration (matching productive board_profile.c) ---- */

static const board_uart_port_t s_gnss_ports[] = {
    BOARD_UART_GNSS_PRIMARY,
    BOARD_UART_GNSS_SECONDARY,
};

int board_profile_get_gnss_port_count(void)
{
    return (int)(sizeof(s_gnss_ports) / sizeof(s_gnss_ports[0]));
}

board_uart_port_t board_profile_get_gnss_port(int index)
{
    if (index < 0 || index >= board_profile_get_gnss_port_count()) {
        return BOARD_UART_COUNT;
    }
    return s_gnss_ports[index];
}
