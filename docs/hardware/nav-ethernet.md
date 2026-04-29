# Ethernet / Network Interface

## Ethernet Hardware

### LilyGO T-Eth Lite ESP32 (classic) — Internal MAC RMII

| Parameter | Value |
|-----------|-------|
| Ethernet type | Internal MAC RMII |
| PHY | Built-in EMAC |
| SPI bus | Not used for Ethernet |

### LilyGO T-Eth Lite ESP32-S3 — W5500 via SPI

| Parameter | Value |
|-----------|-------|
| Ethernet type | W5500 SPI |
| SPI MISO | GPIO13 |
| SPI MOSI | GPIO11 |
| SPI CLK | GPIO12 |
| CS (W5500) | GPIO10 |
| INTR (W5500) | GPIO4 |

## Network Ports

| Port | Default | Purpose |
|------|---------|---------|
| 9999 (UDP) | Configurable | AOG AgIO UDP communication |
| 2101 (TCP) | Configurable | NTRIP caster connection |

Ports are defined in `board_profile.h` via `board_network_ports_t` and
configurable per board. Access via `board_profile_get_network_ports()`.

## Component Architecture

```text
  ┌─────────────────────────────────────────────────┐
  │  Application Layer (ntrip_client, aog_nav_app)  │
  └─────────────────┬───────────────────────────────┘
                    │ rx_read() / tx_write()
  ┌─────────────────┴───────────────────────────────┐
  │  Transport Layer                                  │
  │  ┌──────────────┐    ┌──────────────────────┐   │
  │  │ transport_tcp│    │ transport_udp        │   │
  │  │ RX ring buf  │    │ RX ring buf           │   │
  │  │ TX ring buf  │    │ TX ring buf           │   │
  │  │ partial safe │    │ partial write safe    │   │
  │  │ no NTRIP     │    │ no AOG/PGN            │   │
  │  └──────┬───────┘    └──────────┬───────────┘   │
  └─────────┼────────────────────────┼──────────────┘
            │ injectable HAL ops      │ injectable HAL ops
  ┌─────────┴────────────────────────┴──────────────┐
  │  HAL Ethernet (hal_eth)                           │
  │  - init/deinit                                    │
  │  - get_status (link, IP, MAC, errors)            │
  │  - esp_eth + esp_netif (ESP-IDF)                 │
  └─────────────────────────────────────────────────┘
            │
  ┌─────────┴──────────────────────────────────────┐
  │  Hardware (RMII MAC or W5500 SPI)               │
  └─────────────────────────────────────────────────┘
```

## Buffer Sizes

| Buffer | Size | Location |
|--------|------|----------|
| TCP RX ring buffer | 512 B | `transport_tcp_t.rx_storage[]` |
| TCP TX ring buffer | 512 B | `transport_tcp_t.tx_storage[]` |
| UDP RX ring buffer | 512 B | `transport_udp_t.rx_storage[]` |
| UDP TX ring buffer | 512 B | `transport_udp_t.tx_storage[]` |

## Data Flow

### TCP (NTRIP path)

```text
NTRIP Caster → lwip TCP → HAL recv → transport_tcp RX buffer
                                     → ntrip_client reads

ntrip_client writes RTCM → transport_tcp TX buffer → HAL send → lwip TCP → NTRIP Caster
```

### UDP (AOG path)

```text
AOG/AgIO → lwip UDP → HAL recvfrom → transport_udp RX buffer
                                    → aog_nav_app reads

aog_nav_app writes → transport_udp TX buffer → HAL sendto → lwip UDP → AOG/AgIO
```

## Diagnostics

### TCP Diagnostics (`transport_tcp_diagnostics_t`)

| Field | Description |
|-------|-------------|
| `rx_total` | Cumulative bytes received |
| `tx_total` | Cumulative bytes transmitted |
| `rx_overflows` | RX buffer overflow count |
| `tx_backpressure` | TX partial write / failure events |
| `connect_count` | Total connect attempts |
| `disconnect_count` | Total disconnect events |

### UDP Diagnostics (`transport_udp_diagnostics_t`)

| Field | Description |
|-------|-------------|
| `rx_total` | Cumulative bytes received |
| `tx_total` | Cumulative bytes transmitted |
| `rx_overflows` | RX buffer overflow count |
| `tx_backpressure` | TX partial write / failure events |
| `packets_rx` | Number of datagrams received |
| `packets_tx` | Number of datagrams sent |

### Ethernet Status (`hal_eth_status_t`)

| Field | Description |
|-------|-------------|
| `link_up` | Physical link detected |
| `ip_acquired` | DHCP or static IP configured |
| `ip_addr` | IPv4 address |
| `mac[6]` | MAC address |
| `error_flags` | Bitfield: init_failed, link_lost, dhcp_failed, spi_failed |

## Hardware Smoke Test

Location: `test/hardware/test_ethernet_smoke/`

### Wiring (W5500)

```
ESP32-S3          W5500 Module
─────────         ─────────────
GPIO13 (MISO) ─→  MISO
GPIO11 (MOSI) ─→  MOSI
GPIO12 (CLK)  ─→  SCLK
GPIO10 (CS)   ─→  CS
GPIO4  (INTR) ─→  INTR
3.3V          ─→  VCC
GND           ─→  GND
```

### Test Procedure

```bash
# Build for ESP32-S3 target
pio run -e full_test_esp32s3
pio device monitor -b 115200
```

### Expected Logs

```
I (xxx) HAL_ETH: W5500 SPI Ethernet init (board profile based)
I (xxx) BOARD_PROFILE: Ethernet: 2
```

After DHCP:
```
I (xxx) esp_netif: ip: 192.168.1.xxx, mask: 255.255.255.0, gw: 192.168.1.1
```

## Troubleshooting

### No link detected

1. Check Ethernet cable connection
2. Verify board profile ethernet type matches hardware
3. For W5500: verify SPI wiring and power supply

### DHCP timeout

1. Check network connectivity
2. Try static IP configuration (future enhancement)
3. Check for IP address conflicts

### SPI errors (W5500)

1. Verify CS pin is correct (GPIO10)
2. Check SPI bus frequency (max 10 MHz for W5500)
3. No collision with device SPI bus

### TX backpressure events

- `tx_backpressure` counter increments when send fails or writes partially
- Check network connectivity if counter increases
- May indicate remote endpoint unreachable
