#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Board Type ---- */

typedef enum {
    BOARD_UNKNOWN = 0,
    BOARD_LILYGO_T_ETH_LITE_ESP32,
    BOARD_LILYGO_T_ETH_LITE_ESP32S3
} board_type_t;

/* ---- Ethernet Kind ---- */

typedef enum {
    ETH_UNKNOWN = 0,
    ETH_INTERNAL_MAC_RMII,
    ETH_W5500_SPI
} ethernet_kind_t;

/* ---- UART Ports ---- */

typedef enum {
    BOARD_UART_CONSOLE = 0,
    BOARD_UART_GNSS_PRIMARY,
    BOARD_UART_GNSS_SECONDARY,
    BOARD_UART_COUNT
} board_uart_port_t;

/* ---- UART Pin Configuration ---- */

typedef struct {
    int8_t uart_num;     /* ESP-IDF UART number (UART_NUM_0/1/2) */
    int8_t tx_pin;       /* GPIO number for TX (-1 if unused) */
    int8_t rx_pin;       /* GPIO number for RX (-1 if unused) */
    int8_t rts_pin;      /* GPIO number for RTS (-1 if unused) */
    int8_t cts_pin;      /* GPIO number for CTS (-1 if unused) */
} board_uart_pin_config_t;

/* Get pin config for a UART port. Returns NULL if port not available. */
const board_uart_pin_config_t* board_profile_get_uart_pins(board_uart_port_t port);

/* ---- SPI Bus Roles ---- */

typedef enum {
    BOARD_SPI_ETHERNET = 0,
    BOARD_SPI_DEVICE,
    BOARD_SPI_STORAGE,
    BOARD_SPI_COUNT
} board_spi_bus_t;

/* ---- Feature Flags ---- */

#define BOARD_FEATURE_ETHERNET    (1 << 0)
#define BOARD_FEATURE_UART_GNSS   (1 << 1)
#define BOARD_FEATURE_SPI_DEVICE  (1 << 2)

/* ---- Board Profile API ---- */

board_type_t board_profile_get_board(void);
ethernet_kind_t board_profile_get_eth(void);
bool board_profile_has_uart(board_uart_port_t port);
bool board_profile_has_spi(board_spi_bus_t bus);
unsigned int board_profile_get_features(void);

#ifdef __cplusplus
}
#endif
