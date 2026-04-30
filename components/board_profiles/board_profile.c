#include "esp_log.h"
#include "board_profile.h"

static const char* TAG = "BOARD_PROFILE";

board_type_t board_profile_get_board(void)
{
    board_type_t board = BOARD_UNKNOWN;

#if defined(CONFIG_BOARD_ESP32S3)
    board = BOARD_LILYGO_T_ETH_LITE_ESP32S3;
#elif defined(CONFIG_BOARD_ESP32)
    board = BOARD_LILYGO_T_ETH_LITE_ESP32;
#endif

    ESP_LOGI(TAG, "Board: %d", (int)board);
    return board;
}

ethernet_kind_t board_profile_get_eth(void)
{
    ethernet_kind_t eth = ETH_UNKNOWN;

#if defined(CONFIG_ETH_W5500)
    eth = ETH_W5500_SPI;
#elif defined(CONFIG_ETH_MAC_RMII)
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
#if defined(CONFIG_ETH_W5500)
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

bool board_profile_get_uart_pins(board_uart_port_t port, board_uart_pins_t* pins)
{
    if (pins == NULL) {
        return false;
    }

    pins->tx_pin = -1;
    pins->rx_pin = -1;

    switch (port) {
    case BOARD_UART_CONSOLE:
#if defined(CONFIG_BOARD_ESP32)
        pins->tx_pin = 1;   /* GPIO1: default TX for ESP32 */
        pins->rx_pin = 3;   /* GPIO3: default RX for ESP32 */
#elif defined(CONFIG_BOARD_ESP32S3)
        pins->tx_pin = 43;  /* GPIO43: default TX for ESP32-S3 */
        pins->rx_pin = 44;  /* GPIO44: default RX for ESP32-S3 */
#endif
        return true;

    case BOARD_UART_GNSS_PRIMARY:
#if defined(CONFIG_BOARD_ESP32)
        pins->tx_pin = -1;  /* Not used for GNSS RX-only */
        pins->rx_pin = 16;  /* GPIO16: GNSS primary RX */
#elif defined(CONFIG_BOARD_ESP32S3)
        pins->tx_pin = -1;
        pins->rx_pin = 4;   /* GPIO4: GNSS primary RX on S3 */
#endif
        return true;

    case BOARD_UART_GNSS_SECONDARY:
#if defined(CONFIG_BOARD_ESP32)
        pins->tx_pin = -1;
        pins->rx_pin = 17;  /* GPIO17: GNSS secondary RX */
#elif defined(CONFIG_BOARD_ESP32S3)
        pins->tx_pin = -1;
        pins->rx_pin = 5;   /* GPIO5: GNSS secondary RX on S3 */
#endif
        return true;

    default:
        return false;
    }
}
