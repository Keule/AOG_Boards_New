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
