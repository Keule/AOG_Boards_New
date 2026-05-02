#pragma once

#include <stdint.h>
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

/* ---- Pin Assignment Sentinel ----
 * Indicates a GPIO pin has not been assigned to this function yet.
 * Maps to UART_PIN_NO_CHANGE in ESP-IDF hal_uart_esp32.c.
 * When a UART tx_pin is BOARD_PIN_UNASSIGNED, the UART is effectively
 * RX-only from the application perspective until the physical pin is
 * configured. */
#define BOARD_PIN_UNASSIGNED  (-1)

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

/* ---- UART Pin Configuration ---- */

typedef struct {
    int tx_pin;
    int rx_pin;
} board_uart_pins_t;

/* ---- GNSS Port Iteration (generic, extensible) ----
 *
 * Returns the list of GNSS UART port candidates for RTCM routing.
 * The caller iterates from 0..board_profile_get_gnss_port_count()-1
 * and checks each port with board_profile_has_uart() and
 * board_profile_has_uart_tx() to determine active RTCM targets.
 *
 * Current entries: BOARD_UART_GNSS_PRIMARY, BOARD_UART_GNSS_SECONDARY.
 * Future: BOARD_UART_GNSS_TERTIARY, etc. — just add one line to the
 * static table in board_profile.c. No caller changes needed.
 *
 * Returns BOARD_UART_COUNT (invalid sentinel) for out-of-range index. */
int board_profile_get_gnss_port_count(void);
board_uart_port_t board_profile_get_gnss_port(int index);

/* Get UART pin configuration for a specific port.
 * Returns true if pins are available, false if port is not supported.
 * If port is not supported, *pins is zeroed. */
bool board_profile_get_uart_pins(board_uart_port_t port, board_uart_pins_t* pins);

/* Check if a UART port has a valid (assigned) TX pin.
 * Returns true if the port exists AND tx_pin != BOARD_PIN_UNASSIGNED.
 * Used by RTCM router to decide whether to register an output
 * for a given GNSS UART port. */
bool board_profile_has_uart_tx(board_uart_port_t port);

#ifdef __cplusplus
}
#endif
