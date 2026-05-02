# NAV-BOARD-PINS-001: LilyGO T-ETH-Lite ESP32 Pinmatrix

## Overview

This document defines the complete GPIO pin assignment for the LilyGO T-ETH-Lite ESP32 board when configured for the **NAV (Navigation)** role.

**Reference:** Keule/ESP32_AGO_GNSS `include/board_profile/LILYGO_T_ETH_LITE_ESP32_board_pins.h`

## Pin Matrix

### GNSS UART Pins

| Signal | UART | TX Pin | RX Pin | Baudrate | Notes |
|--------|------|--------|--------|----------|-------|
| GNSS Primary (UM980 #1) | UART_NUM_1 | GPIO2 | GPIO4 | 921600 | TX=GPIO2 output-capable, RX=GPIO4 |
| GNSS Secondary (UM980 #2) | UART_NUM_2 | GPIO33 | GPIO35 | 921600 | TX=GPIO33 output-capable, RX=GPIO35 input-only |

### Ethernet RMII Pins (RTL8201)

| Signal | Pin | Notes |
|--------|-----|-------|
| RMII Clock | ETH_CLOCK_GPIO0_IN | External 50 MHz oscillator on GPIO0 |
| MDC | GPIO23 | Management Data Clock |
| MDIO | GPIO18 | Management Data I/O |
| Power | GPIO12 | PHY power control (active high) |
| Reset | — | No reset pin (BOARD_PIN_UNASSIGNED = -1) |
| PHY Address | 0 | RTL8201 at I2C address 0 |
| PHY Type | RTL8201 | Internal MAC, RMII mode |

### SD Card Pins (SPI Mode)

| Signal | Pin | Notes |
|--------|-----|-------|
| MISO | GPIO34 | Input-only GPIO, OK for SPI MISO |
| MOSI | GPIO13 | |
| SCLK | GPIO14 | |
| CS | GPIO5 | |

### Miscellaneous Pins

| Signal | Pin | Notes |
|--------|-----|-------|
| Safety Input | GPIO15 | Safety signal input (active low) |
| Log Switch | GPIO0 | Boot strapping pin, enables serial log |

## GPIO Constraints (Classic ESP32)

- **GPIO 0–33**: Input + Output capable — can be used for TX or RX
- **GPIO 34–39**: INPUT ONLY — cannot be used as UART TX
- **GPIO35** is explicitly NOT used as TX on any UART (hard rule)

## Boot Log

At startup, the board prints the complete pin profile:

```
BOARD: profile=lilygo_t_eth_lite_esp32 role=NAV
BOARD: eth=RTL8201/RMII clk=GPIO0_IN mdc=23 mdio=18 power=12 reset=-1
BOARD: gnss1 uart=1 baud=921600 tx=2 rx=4
BOARD: gnss2 uart=2 baud=921600 tx=33 rx=35
BOARD: sd miso=34 mosi=13 sclk=14 cs=5
BOARD: safety_in=15 log_switch=0
=== BOARD PIN SANITY CHECK: PASS ===
```

## Pin Sanity Check

At boot, the following checks are performed:

1. **UART TX not input-only**: All TX pins must be GPIO 0–33 (not 34–39)
2. **GPIO35 not TX**: GPIO35 must not be used as TX on any UART
3. **NAV Ethernet type**: Must be RMII (not W5500) for NAV role
4. **GNSS pins assigned**: Primary and Secondary UART must have valid pins

If any check fails, `BOARD_PIN_ERROR` is logged and init is aborted.

---

## Wo werden die Pins künftig geändert?

| Zweck | Datei | Zeile/Bereich | Symbol / Struktur | Bemerkung |
|---|---|---:|---|---|
| GNSS primary UART TX/RX | `components/board_profiles/board_profile.h` | `BOARD_GNSS_UART1_TX_PIN` / `BOARD_GNSS_UART1_RX_PIN` | `#define BOARD_GNSS_UART1_TX_PIN 2` | Quelle der Wahrheit. Wird von `board_profile_get_uart_pins()` gelesen. |
| GNSS secondary UART TX/RX | `components/board_profiles/board_profile.h` | `BOARD_GNSS_UART2_TX_PIN` / `BOARD_GNSS_UART2_RX_PIN` | `#define BOARD_GNSS_UART2_TX_PIN 33` | Quelle der Wahrheit. Wird von `board_profile_get_uart_pins()` gelesen. |
| GNSS UART Baudrate | `components/board_profiles/board_profile.h` | `BOARD_GNSS_UART_BAUDRATE` | `#define BOARD_GNSS_UART_BAUDRATE 921600` | Zentrale Definition. Wird auch in `app_core.c` (UM980_BAUDRATE) referenziert. |
| Ethernet RMII PHY/Pins | `components/board_profiles/board_profile.h` | `BOARD_ETH_*` Defines | `BOARD_ETH_MDC_PIN`, `BOARD_ETH_MDIO_PIN`, etc. | Quelle der Wahrheit. Wird von `board_profile_get_eth_pins()` gelesen. |
| Ethernet RMII Clock Mode | `components/board_profiles/board_profile.h` | `BOARD_ETH_CLK_MODE` | `#define BOARD_ETH_CLK_MODE 0` | ETH_CLOCK_GPIO0_IN |
| SD Pins | `components/board_profiles/board_profile.h` | `BOARD_SD_*` Defines | `BOARD_SD_MISO_PIN`, `BOARD_SD_MOSI_PIN`, etc. | Quelle der Wahrheit. Wird von `board_profile_get_sd_pins()` gelesen. |
| Safety Input | `components/board_profiles/board_profile.h` | `BOARD_SAFETY_IN_PIN` | `#define BOARD_SAFETY_IN_PIN 15` | Quelle der Wahrheit. Wird von `board_profile_get_misc_pins()` gelesen. |
| Log Switch | `components/board_profiles/board_profile.h` | `BOARD_LOG_SWITCH_PIN` | `#define BOARD_LOG_SWITCH_PIN 0` | Quelle der Wahrheit. Wird von `board_profile_get_misc_pins()` gelesen. |
| Console UART TX/RX | `components/board_profiles/board_profile.c` | `board_profile_get_uart_pins()` CASE `BOARD_UART_CONSOLE` | Inline-Defines (GPIO1/GPIO3 für ESP32) | Nur hier definiert, kein #define im Header. |
| GNSS2 TX Sanity Check | `components/app_core/app_core.c` | `board_pin_sanity_check()` | `ESP32_INPUT_ONLY_GPIO_MIN/MAX`, `pins.tx_pin == 35` | Prüft dass TX-Pins nicht input-only sind. |
| Pin Boot-Log | `components/app_core/app_core.c` | `board_pin_boot_log()` | `BOARD_PROFILE_NAME`, accessor calls | Druckt alle Pins beim Boot. |
| Test Mock Pins | `lib/test_mocks/board_profile.h` + `board_profile.c` | Identische `BOARD_*` Defines | Must match productive header 1:1 | Mock repliziert die produktiven Defines exakt. |

### Stable Search Anchors

When searching for pin definitions, use these stable anchors:

```
BOARD_PROFILE_NAME
BOARD_GNSS_UART1_TX_PIN
BOARD_GNSS_UART1_RX_PIN
BOARD_GNSS_UART2_TX_PIN
BOARD_GNSS_UART2_RX_PIN
BOARD_GNSS_UART_BAUDRATE
BOARD_ETH_MDC_PIN
BOARD_ETH_MDIO_PIN
BOARD_ETH_POWER_PIN
BOARD_ETH_RESET_PIN
BOARD_SD_MISO_PIN
BOARD_SD_MOSI_PIN
BOARD_SD_SCLK_PIN
BOARD_SD_CS_PIN
BOARD_SAFETY_IN_PIN
BOARD_LOG_SWITCH_PIN
```

## Changes Made (NAV-BOARD-PINS-001)

| File | Change |
|------|--------|
| `board_profile.h` | Added ETH/SD/MISC pin defines, pin structures, accessor declarations |
| `board_profile.c` | Fixed GNSS pins (14/16→2/4, 15/17→33/35), added ETH/SD/MISC accessors |
| `lib/test_mocks/board_profile.h` | Synced with productive header (all new defines + structures) |
| `lib/test_mocks/board_profile.c` | Updated GNSS pins, added ETH/SD/MISC mock accessors |
| `app_core.c` | Added `board_pin_boot_log()`, `board_pin_sanity_check()` at init |
| `test_board_profile_smoke.c` | Added 9 new tests (pin values, ETH/SD/MISC, input-only checks) |
