#include "esp_log.h"
#include "board_profile.h"

static const char* TAG = "BOARD_PROFILE";

board_type_t board_profile_get_board(void)
{
    board_type_t board = BOARD_UNKNOWN;

#if defined(BOARD_LILYGO_T_ETH_LITE_ESP32S3)
    board = BOARD_LILYGO_T_ETH_LITE_ESP32S3;
#elif defined(BOARD_LILYGO_T_ETH_LITE_ESP32)
    board = BOARD_LILYGO_T_ETH_LITE_ESP32;
#endif

    ESP_LOGI(TAG, "Board: %d", (int)board);
    return board;
}

ethernet_kind_t board_profile_get_eth(void)
{
    ethernet_kind_t eth = ETH_UNKNOWN;

#if defined(ETH_KIND_W5500_SPI)
    eth = ETH_W5500_SPI;
#elif defined(ETH_KIND_INTERNAL_MAC_RMII)
    eth = ETH_INTERNAL_MAC_RMII;
#endif

    ESP_LOGI(TAG, "Ethernet: %d", (int)eth);
    return eth;
}

bool board_profile_has_uart(board_uart_port_t port)
{
    switch (port) {
    case BOARD_UART_CONSOLE:
        return true;

    case BOARD_UART_GNSS_PRIMARY:
    case BOARD_UART_GNSS_SECONDARY:
#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)
        return true;
#else
        return false;
#endif

    default:
        return false;
    }
}

bool board_profile_has_spi(board_spi_bus_t bus)
{
    switch (bus) {
    case BOARD_SPI_ETHERNET:
#if defined(ETH_KIND_W5500_SPI)
        return true;
#else
        return false;
#endif

    case BOARD_SPI_DEVICE:
#if defined(DEVICE_ROLE_STEERING) || defined(DEVICE_ROLE_FULL_TEST)
        return true;
#else
        return false;
#endif

    case BOARD_SPI_STORAGE:
        /* No board currently has storage SPI */
        return false;

    default:
        return false;
    }
}

unsigned int board_profile_get_features(void)
{
    unsigned int features = 0;

    /* Ethernet is always available on all boards */
    features |= BOARD_FEATURE_ETHERNET;

#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)
    features |= BOARD_FEATURE_UART_GNSS;
#endif

#if defined(DEVICE_ROLE_STEERING) || defined(DEVICE_ROLE_FULL_TEST)
    features |= BOARD_FEATURE_SPI_DEVICE;
#endif

    return features;
}
