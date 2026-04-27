#pragma once

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

board_type_t board_profile_get_board(void);
ethernet_kind_t board_profile_get_eth(void);
