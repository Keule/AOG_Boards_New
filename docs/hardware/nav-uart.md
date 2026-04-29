# UART Navigation Interface

Overview of the UART transport layer used for GNSS receiver communication
in the AOG ESP Multiboard navigation subsystem.

## UART Pin Assignments

### LilyGO T-Eth Lite ESP32 (classic)

| UART Port    | ESP-IDF UART | TX Pin | RX Pin | Purpose         |
|:-------------|:-------------|:-------|:-------|:----------------|
| Console      | UART_NUM_0   | GPIO1  | GPIO3  | Debug/monitor   |
| GNSS Primary | UART_NUM_1   | GPIO33 | GPIO32 | UM980 #1        |
| GNSS Secondary | UART_NUM_2 | GPIO4  | GPIO2  | UM980 #2        |

### LilyGO T-Eth Lite ESP32-S3

| UART Port    | ESP-IDF UART | TX Pin | RX Pin | Purpose         |
|:-------------|:-------------|:-------|:-------|:----------------|
| Console      | UART_NUM_0   | GPIO43 | GPIO44 | Debug/monitor   |
| GNSS Primary | UART_NUM_1   | GPIO17 | GPIO18 | UM980 #1        |
| GNSS Secondary | UART_NUM_2 | GPIO47 | GPIO48 | UM980 #2        |

## Configuration

| Parameter              | Value   |
|:-----------------------|:--------|
| Baudrate               | 921600  |
| Data bits              | 8       |
| Stop bits              | 1       |
| Parity                 | None    |
| Flow control (RTS/CTS) | None    |

## Buffer Sizes

| Buffer                    | Size   | Location                        |
|:--------------------------|:-------|:--------------------------------|
| Transport RX ring buffer  | 1024 B | `transport_uart_t.rx_storage[]` |
| Transport TX ring buffer  | 512 B  | `transport_uart_t.tx_storage[]` |
| ESP-IDF UART driver RX    | 1024 B | `uart_driver_install()`         |
| ESP-IDF UART driver TX    | 512 B  | `uart_driver_install()`         |

Total RX buffering per port: **2048 bytes** (512 driver + 1024 transport service_step reads in 128-byte bursts).

Total TX buffering per port: **1024 bytes** (512 transport ring + 512 driver).

## Data Flow

```
                      NAV-IO-001 UART Data Flow
  ─────────────────────────────────────────────────────────────

  ┌──────────────┐     UART RX     ┌──────────────────────────┐
  │              │  921600 baud    │  ESP-IDF UART Driver     │
  │   UM980 #1   │ ──────────────→ │  (RX buffer: 1024 B)     │
  │  (GNSS PRI)  │                 └────────────┬─────────────┘
  │              │                              │ hal_uart_read()
  └──────────────┘                              │ (nonblocking)
                                                 ▼
                                   ┌──────────────────────────┐
                                   │  transport_uart_t        │
                                   │  ┌──────────────────┐   │
                                   │  │ rx_buffer (1024 B)│  │
                                   │  └────────┬─────────┘   │
                                   │           │              │
                                   │  service_step()          │
                                   │  128-byte bursts        │
                                   └────────────┬─────────────┘
                                                │ transport_uart_rx_read()
                                                ▼
                                   ┌──────────────────────────┐
                                   │  gnss_um980_t            │
                                   │  NMEA parser             │
                                   │  → GGA, RMC, GSA, etc.   │
                                   └──────────────────────────┘

  ┌──────────────────────────┐
  │  RTCM Router / NTRIP     │
  │  (RTCM correction data)  │
  └────────────┬─────────────┘
               │ transport_uart_tx_write()
               ▼
  ┌──────────────────────────┐
  │  transport_uart_t        │
  │  ┌──────────────────┐   │
  │  │ tx_buffer (512 B) │   │
  │  └────────┬─────────┘   │
  └───────────┼──────────────┘
              │ service_step() drains
              │ 128-byte bursts
              ▼
  ┌──────────────────────────┐      UART TX     ┌──────────────┐
  │  ESP-IDF UART Driver     │ ──────────────→ │              │
  │  (TX buffer: 512 B)      │  921600 baud    │   UM980 #1   │
  └──────────────────────────┘                 │  (GNSS PRI)  │
                                              └──────────────┘
```

The same flow applies to UM980 #2 (GNSS Secondary) on a separate UART port.

## Statistics

The `transport_uart_t` struct tracks per-port statistics:

| Field          | Type     | Description                              |
|:---------------|:---------|:-----------------------------------------|
| `rx_total`     | `uint32` | Cumulative bytes received from HAL       |
| `tx_total`     | `uint32` | Cumulative bytes transmitted via HAL     |
| `rx_overflows` | `uint32` | RX ring buffer overflow count            |

Access via `transport_uart_get_rx_stats()` and `transport_uart_get_tx_stats()`.

## Hardware Smoke Test

A hardware smoke test is available at `test/hardware/test_uart_um980_smoke/`.

### Wiring

Connect UM980 GNSS receivers to the ESP32:

```
  UM980 Primary            ESP32 (classic)
  ────────────             ──────────────
  TX          ─────────→   GPIO32 (RX1)
  RX          ←─────────   GPIO33 (TX1)
  VCC         ─────────→   3.3V
  GND         ─────────→   GND
```

### Build and Run

```bash
# Build for ESP32 target
pio test -e nav_esp32_t_eth_lite -f "hardware/*"

# Or flash and monitor directly
pio run -e nav_esp32_t_eth_lite
pio device monitor -b 115200
```

### Expected NMEA Output

The UM980 at 921600 baud outputs standard NMEA sentences:

```
$GNGGA,092750.000,5243.1234,N,01322.5678,E,1,12,0.9,34.5,M,47.0,M,,*4A
$GNRMC,092750.00,A,5243.1234,N,01322.5678,E,0.5,45.3,290424,,,A*4E
$GNGSA,A,3,01,05,07,12,15,20,24,28,31,1.0,0.9,0.8*2A
```

Format: `$<talker><sentence>,<fields>*<checksum>\r\n`

- `$` — start of sentence
- `<talker>` — GN (combined GPS+BeiDou), GP (GPS), BD (BeiDou)
- `<sentence>` — GGA, RMC, GSA, GSV, etc.
- `*<checksum>` — XOR of all bytes between `$` and `*`, 2-digit hex

## Troubleshooting

### No data received at 921600 baud

1. **Verify baudrate**: UM980 ships at 115200 by default. Configure it to 921600 via:
   ```
   $PCAS,01,0,921600,8,1,0*1D
   ```
2. **Check wiring**: TX→RX and RX→TX must be crossed. Verify with a logic analyzer.
3. **Signal integrity**: At 921600 baud, keep wires short (<30 cm). Use twisted pair if possible.
4. **Power supply**: UM980 draws ~100 mA peak. Ensure stable 3.3V.

### RX buffer overflows

- `rx_overflows` counter increments when the 1024-byte ring buffer fills faster
  than `service_step()` can drain it.
- At 921600 baud, max data rate is ~92 KB/s. The service_step runs at ~1 kHz,
  reading 128 bytes per call, yielding ~128 KB/s drain capacity — sufficient margin.
- If overflows occur, increase `TRANSPORT_UART_RX_BUFFER_SIZE` in `transport_uart.h`.

### CRC/checksum errors on NMEA

- Ensure 8N1 configuration (8 data bits, no parity, 1 stop bit).
- Check for ground loops between ESP32 and UM980 power supplies.
- At 921600 baud, cable capacitance can distort edges — use shorter cables.

### Double UART init warning

The HAL tracks port initialization state. If `hal_uart_port_init()` is called
twice for the same port, it logs a warning and returns `HAL_OK` without
re-initializing the driver.
