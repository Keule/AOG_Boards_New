#include "board_profile.h"

#if defined(ESP_PLATFORM)
#include "esp_log.h"
static const char* TAG = "BOARD_PROFILE";
#endif

board_type_t board_profile_get_board(void)
{
    board_type_t board = BOARD_UNKNOWN;

#if defined(CONFIG_BOARD_ESP32S3)
    board = BOARD_LILYGO_T_ETH_LITE_ESP32S3;
#elif defined(CONFIG_BOARD_ESP32)
    board = BOARD_LILYGO_T_ETH_LITE_ESP32;
#endif

#if defined(ESP_PLATFORM)
    ESP_LOGI(TAG, "Board: %d", (int)board);
#endif
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

#if defined(ESP_PLATFORM)
    ESP_LOGI(TAG, "Ethernet: %d", (int)eth);
#endif
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

const board_uart_pin_config_t* board_profile_get_uart_pins(board_uart_port_t port)
{
    if (port < 0 || port >= BOARD_UART_COUNT) {
        return NULL;
    }
    if (!board_profile_has_uart(port)) {
        return NULL;
    }

#if defined(CONFIG_BOARD_ESP32)
    /* LilyGO T-Eth Lite ESP32 (classic) pin assignments */
    static const board_uart_pin_config_t esp32_pins[BOARD_UART_COUNT] = {
        [BOARD_UART_CONSOLE]       = { .uart_num = 0, .tx_pin = 1,  .rx_pin = 3,  .rts_pin = -1, .cts_pin = -1 },
        [BOARD_UART_GNSS_PRIMARY]  = { .uart_num = 1, .tx_pin = 33, .rx_pin = 32, .rts_pin = -1, .cts_pin = -1 },
        [BOARD_UART_GNSS_SECONDARY]= { .uart_num = 2, .tx_pin = 4,  .rx_pin = 2,  .rts_pin = -1, .cts_pin = -1 },
    };
    return &esp32_pins[port];

#elif defined(CONFIG_BOARD_ESP32S3)
    /* LilyGO T-Eth Lite ESP32-S3 pin assignments */
    static const board_uart_pin_config_t esp32s3_pins[BOARD_UART_COUNT] = {
        [BOARD_UART_CONSOLE]       = { .uart_num = 0, .tx_pin = 43, .rx_pin = 44, .rts_pin = -1, .cts_pin = -1 },
        [BOARD_UART_GNSS_PRIMARY]  = { .uart_num = 1, .tx_pin = 17, .rx_pin = 18, .rts_pin = -1, .cts_pin = -1 },
        [BOARD_UART_GNSS_SECONDARY]= { .uart_num = 2, .tx_pin = 47, .rx_pin = 48, .rts_pin = -1, .cts_pin = -1 },
    };
    return &esp32s3_pins[port];

#else
    (void)port;
    return NULL;
#endif
}

const board_spi_pin_config_t* board_profile_get_spi_pins(board_spi_bus_t bus)
{
    if (bus < 0 || bus >= BOARD_SPI_COUNT) {
        return NULL;
    }
    if (!board_profile_has_spi(bus)) {
        return NULL;
    }

#if defined(CONFIG_BOARD_ESP32)
    /* LilyGO T-Eth Lite ESP32 (classic) — internal MAC RMII, no SPI for ETH */
    (void)bus;
    return NULL;

#elif defined(CONFIG_BOARD_ESP32S3)
    /* LilyGO T-Eth Lite ESP32-S3 — W5500 via SPI */
    static const board_spi_pin_config_t esp32s3_spi[BOARD_SPI_COUNT] = {
        [BOARD_SPI_ETHERNET] = {
            .miso_pin = 13,
            .mosi_pin = 11,
            .sclk_pin = 12,
            .cs_pin   = 10,
            .intr_pin = 4
        },
        /* BOARD_SPI_DEVICE and BOARD_SPI_STORAGE: no pins assigned */
    };
    return &esp32s3_spi[bus];

#else
    (void)bus;
    return NULL;
#endif
}

const board_network_ports_t* board_profile_get_network_ports(void)
{
    /* Same defaults for all boards */
    static const board_network_ports_t s_ports = {
        .aog_udp_port    = 9999,
        .ntrip_tcp_port  = 2101,
    };
    return &s_ports;
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
