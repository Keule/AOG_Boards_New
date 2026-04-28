#pragma once

#include <stdbool.h>

typedef enum {
    BOARD_UNKNOWN = 0,
    BOARD_LILYGO_T_ETH_LITE_ESP32,
    BOARD_LILYGO_T_ETH_LITE_ESP32S3
} board_type_t;

typedef enum {
    ETH_UNKNOWN = 0,
    ETH_INTERNAL_MAC_RMII,
    ETH_W5500_SPI
} ethernet_kind_t;

typedef enum {
    BOARD_UART_CONSOLE = 0,
    BOARD_UART_GNSS_PRIMARY,
    BOARD_UART_GNSS_SECONDARY
} board_uart_port_t;

typedef enum {
    BOARD_SPI_ROLE_ETHERNET = 0,
    BOARD_SPI_ROLE_DEVICE,
    BOARD_SPI_ROLE_STORAGE
} board_spi_role_t;

typedef enum {
    BOARD_FEATURE_ETHERNET_REQUIRED = 0,
    BOARD_FEATURE_UART,
    BOARD_FEATURE_SPI,
    BOARD_FEATURE_NVS,
    BOARD_FEATURE_OTA
} board_feature_t;

board_type_t board_profile_get_board(void);
ethernet_kind_t board_profile_get_eth(void);
bool board_profile_has_uart(board_uart_port_t port);
bool board_profile_has_spi_role(board_spi_role_t role);
bool board_profile_supports_feature(board_feature_t feature);
