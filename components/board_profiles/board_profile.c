#include "board_profile.h"

board_type_t board_profile_get_board(void)
{
#if defined(BOARD_LILYGO_T_ETH_LITE_ESP32S3)
    return BOARD_LILYGO_T_ETH_LITE_ESP32S3;
#elif defined(BOARD_LILYGO_T_ETH_LITE_ESP32)
    return BOARD_LILYGO_T_ETH_LITE_ESP32;
#else
    return BOARD_UNKNOWN;
#endif
}

ethernet_kind_t board_profile_get_eth(void)
{
#if defined(ETH_KIND_W5500_SPI)
    return ETH_W5500_SPI;
#elif defined(ETH_KIND_INTERNAL_MAC_RMII)
    return ETH_INTERNAL_MAC_RMII;
#else
    return ETH_UNKNOWN;
#endif
}

bool board_profile_has_uart(board_uart_port_t port)
{
    switch (port) {
        case BOARD_UART_CONSOLE:
        case BOARD_UART_GNSS_PRIMARY:
        case BOARD_UART_GNSS_SECONDARY:
            return true;
        default:
            return false;
    }
}

bool board_profile_has_spi_role(board_spi_role_t role)
{
    ethernet_kind_t eth = board_profile_get_eth();

    switch (role) {
        case BOARD_SPI_ROLE_ETHERNET:
            return (eth == ETH_W5500_SPI);
        case BOARD_SPI_ROLE_DEVICE:
            return true;
        case BOARD_SPI_ROLE_STORAGE:
            return true;
        default:
            return false;
    }
}

bool board_profile_supports_feature(board_feature_t feature)
{
    switch (feature) {
        case BOARD_FEATURE_ETHERNET_REQUIRED:
            return (board_profile_get_eth() != ETH_UNKNOWN);
        case BOARD_FEATURE_UART:
            return true;
        case BOARD_FEATURE_SPI:
            return true;
        case BOARD_FEATURE_NVS:
            return true;
        case BOARD_FEATURE_OTA:
            return true;
        default:
            return false;
    }
}
