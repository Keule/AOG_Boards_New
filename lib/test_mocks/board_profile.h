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

/* ---- Board Profile Name (search anchor for pin changes) ---- */

#define BOARD_PROFILE_NAME   "lilygo_t_eth_lite_esp32"

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

/* ---- GNSS UART Baudrate ---- */

#ifndef GNSS_UART_BAUD_OVERRIDE
#define BOARD_GNSS_UART_BAUDRATE  921600
#else
#define BOARD_GNSS_UART_BAUDRATE  GNSS_UART_BAUD_OVERRIDE
#endif

/* ================================================================
 * PIN DEFINITIONS — LilyGO T-ETH-Lite ESP32 (NAV)
 *
 * Source of truth: this section.
 * Reference: Keule/ESP32_AGO_GNSS LILYGO_T_ETH_LITE_ESP32_board_pins.h
 *
 * To change pins, ONLY edit the #defines below.
 * All consumers fetch pins via board_profile_get_uart_pins() or
 * the board_eth_pins_t / board_sd_pins_t / board_misc_pins_t accessors.
 * ================================================================ */

/* ---- GNSS UART Pins (classic ESP32, NAV role) ---- */

#define BOARD_GNSS_UART1_TX_PIN   2    /* UM980 #1 RTCM RXIN ← ESP32 TX */
#define BOARD_GNSS_UART1_RX_PIN   4    /* UM980 #1 NMEA TX → ESP32 RX   */
#define BOARD_GNSS_UART2_TX_PIN   33   /* UM980 #2 RTCM RXIN ← ESP32 TX */
#define BOARD_GNSS_UART2_RX_PIN   34   /* UM980 #2 NMEA TX → ESP32 RX   */
/* NOTE: GPIO34 was previously SD_MISO (BOARD_SD_MISO_PIN).
 * GPIO35 removed due to observed UART instability. */

/* ---- Ethernet RMII Pins (classic ESP32, RTL8201) ---- */

#define BOARD_ETH_PHY_TYPE        0    /* ETH_PHY_RTL8201 */
#define BOARD_ETH_PHY_ADDR        0
#define BOARD_ETH_CLK_MODE        0    /* ETH_CLOCK_GPIO0_IN */
#define BOARD_ETH_RESET_PIN       (-1) /* No reset pin */
#define BOARD_ETH_MDC_PIN         23
#define BOARD_ETH_MDIO_PIN        18
#define BOARD_ETH_POWER_PIN       12

/* ---- SD Card Pins (SPI mode) ---- */

#define BOARD_SD_MISO_PIN         34
#define BOARD_SD_MOSI_PIN         13
#define BOARD_SD_SCLK_PIN         14
#define BOARD_SD_CS_PIN           5

/* ---- Miscellaneous Pins ---- */

#define BOARD_SAFETY_IN_PIN       15   /* Safety input (active low) */
#define BOARD_LOG_SWITCH_PIN      0    /* Log enable (boot strapping) */

/* ================================================================
 * PIN STRUCTURES (for accessor API)
 * ================================================================ */

typedef struct {
    int tx_pin;
    int rx_pin;
} board_uart_pins_t;

typedef struct {
    int phy_type;       /* 0=RTL8201, 1=IP101, etc. (esp_eth_phy_type_t) */
    int phy_addr;       /* I2C address of PHY */
    int clk_mode;       /* RMII clock mode (ETH_CLOCK_GPIO0_IN etc.) */
    int reset_pin;      /* -1 = none */
    int mdc_pin;
    int mdio_pin;
    int power_pin;      /* -1 = none */
} board_eth_pins_t;

typedef struct {
    int miso_pin;
    int mosi_pin;
    int sclk_pin;
    int cs_pin;
} board_sd_pins_t;

typedef struct {
    int safety_in_pin;
    int log_switch_pin;
} board_misc_pins_t;

/* ================================================================
 * BOARD PROFILE API
 * ================================================================ */

board_type_t board_profile_get_board(void);
ethernet_kind_t board_profile_get_eth(void);
bool board_profile_has_uart(board_uart_port_t port);
bool board_profile_has_spi(board_spi_bus_t bus);
unsigned int board_profile_get_features(void);

/* ---- UART Pin Configuration ---- */

/* Get UART pin configuration for a specific port.
 * Returns true if pins are available, false if port is not supported.
 * If port is not supported, *pins is zeroed. */
bool board_profile_get_uart_pins(board_uart_port_t port, board_uart_pins_t* pins);

/* ---- GNSS Port Iteration (generic, extensible) ---- */

int board_profile_get_gnss_port_count(void);
board_uart_port_t board_profile_get_gnss_port(int index);

/* Check if a UART port has a valid (assigned) TX pin. */
bool board_profile_has_uart_tx(board_uart_port_t port);

/* ---- Ethernet Pin Accessor ---- */
bool board_profile_get_eth_pins(board_eth_pins_t* pins);

/* ---- SD Pin Accessor ---- */
bool board_profile_get_sd_pins(board_sd_pins_t* pins);

/* ---- Misc Pin Accessor ---- */
bool board_profile_get_misc_pins(board_misc_pins_t* pins);

#ifdef __cplusplus
}
#endif
