/* Mock board_profile implementation for native/host builds.
 *
 * Provides the same API as components/board_profiles/board_profile.c
 * but without any ESP-IDF dependencies (no esp_log.h, no ESP_LOG macros).
 *
 * This file is compiled ONLY for native tests via extra_scripts/native_test.py
 * and overrides the real board_profile.c.                            */

#include "board_profile.h"

board_type_t board_profile_get_board(void)
{
    return BOARD_LILYGO_T_ETH_LITE_ESP32;
}

ethernet_kind_t board_profile_get_eth(void)
{
    return ETH_W5500_SPI;
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
