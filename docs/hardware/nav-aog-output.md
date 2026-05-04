# NAV-AOG-001: AOG GPS & Heading Output

## Status: Implemented

## Overview

The AOG Navigation App component produces PGN 214 frames at 20 Hz,
carrying GPS position and dual-antenna heading data to AgOpenGPS/AgIO.

## Architecture

```
┌─────────────────┐     ┌──────────────────────┐     ┌────────────────┐
│ gnss_um980      │     │ gnss_dual_heading    │     │                │
│ (primary GNSS)  │────►│ (dual-antenna calc)  │────►│                │
│ → GGA snapshot  │     │ → heading snapshot   │     │                │
│ → position_snap │     │ → heading_snap       │     │                │
└─────────────────┘     └──────────────────────┘     │                │
                                                        ▼
                                              ┌──────────────────────┐
                                              │ aog_navigation_app   │
                                              │                      │
                                              │ → PGN 214 at 20 Hz   │
                                              │ → Hello on request   │
                                              └──────────┬───────────┘
                                                         │
                                                         ▼
                                              ┌──────────────────────┐
                                              │ byte_ring_buffer     │
                                              │ (AOG TX Queue)       │
                                              └──────────┬───────────┘
                                                         │
                                                         ▼
                                              ┌──────────────────────┐
                                              │ transport_udp        │
                                              │ (physical send)      │
                                              └──────────────────────┘
```

## Component: `aog_navigation_app`

### Inputs (via snapshot buffers, NOT owned)

| Input            | Type                  | Source                      |
|------------------|-----------------------|-----------------------------|
| position_source  | `snapshot_buffer_t*`  | gnss_um980 primary GGA      |
| heading_source   | `snapshot_buffer_t*`  | gnss_dual_heading snapshot  |
| aog_rx_source    | `byte_ring_buffer_t*` | transport_udp RX            |

### Outputs

| Output           | Type                  | Consumer            |
|------------------|-----------------------|---------------------|
| aog_tx_dest      | `byte_ring_buffer_t*` | transport_udp TX    |

### PGN Output

| PGN  | Name            | Rate  | Trigger       |
|------|-----------------|-------|---------------|
| 214  | GPS + Heading   | 20 Hz | Periodic      |
| 254  | Hello Response  | —     | On request    |

### PGN 214 Field Mapping

| PGN 214 Field    | Source                                       | Sentinel       |
|------------------|----------------------------------------------|----------------|
| longitude        | nmea_gga_t.longitude                          | DBL_MAX        |
| latitude         | nmea_gga_t.latitude                           | DBL_MAX        |
| altitude         | nmea_gga_t.altitude                           | FLT_MAX        |
| satellites       | nmea_gga_t.num_sats                           | UINT16_MAX     |
| fix_quality      | nmea_gga_t.fix_quality                        | 0              |
| hdop ×100        | nmea_gga_t.hdop * 100                         | UINT16_MAX     |
| age ×100         | nmea_gga_t.age_diff * 100 (if valid)          | UINT16_MAX     |
| heading_dual     | gnss_heading_snapshot_t.heading_deg → rad     | FLT_MAX        |
| heading_true     | (reserved, sentinel)                          | FLT_MAX        |
| speed            | (reserved, sentinel)                          | FLT_MAX        |
| roll             | (reserved, sentinel)                          | FLT_MAX        |
| imu_*            | (reserved, sentinel)                          | MAX values     |

### Hello/Discovery

- Listens for PGN 253 (Hello Request) on the RX buffer
- Responds with PGN 254 containing hardcoded IP/port
- Does NOT send Hello cyclically — only on request

## Transport Path

### Ports

| Direction | Protocol | Local Port | Remote Port |
|-----------|----------|------------|-------------|
| AOG       | UDP      | 9999       | 9999        |

### Wiring (in `app_core.c`)

```c
aog_nav_app_set_position_source(&s_nav_app,
    gnss_um980_get_position_snapshot(&s_primary_gnss));
aog_nav_app_set_heading_source(&s_nav_app,
    gnss_dual_heading_get_snapshot(&s_heading));
aog_nav_app_set_aog_rx_source(&s_nav_app, &s_aog_udp.rx_buffer);
aog_nav_app_set_aog_tx_dest(&s_nav_app, &s_aog_udp.tx_buffer);
```

## Hard Rules

- ✅ AOG app does NOT call `sendto()` / `transport_udp_send()`
- ✅ Transport knows no PGN logic
- ✅ PGN encoding in `protocol_aog`, not in transport
- ✅ No UART/NTRIP changes
- ✅ No steering changes
- ✅ No app_core monolith

## Testing with AgIO/AOG

### Prerequisites

1. ESP32 board connected via Ethernet (W5500)
2. Primary GNSS antenna with RTK fix
3. Secondary GNSS antenna for dual-antenna heading
4. NTRIP correction source active
5. AgIO running on PC on same network

### Test Procedure

1. Flash firmware with `DEVICE_ROLE_NAVIGATION`
2. Power on and verify GNSS fixes on serial console
3. Start AgIO → Module should appear in Network tab
4. Verify PGN 214 data in AgIO:
   - Position lat/lon updates
   - Heading (dual antenna) updates
   - Fix quality shows RTK Fixed
5. Verify no PGN 200/201 in traffic capture (replaced by 214)

### Expected PGN 214 Frame (hex)

```
80 81 35 D6 00 [51 bytes payload] [CRC]

Offset 3-4: D6 00 (PGN 214 LE)
Offset 5:   35 (length = 51 + 2 = 53... wait)
```

Actually: length byte = 2 + data_length = 2 + 51 = 53 = 0x35

## Future Work

- `heading_true`: RMC course over ground (when RMC available)
- `speed`: RMC speed in m/s
- `roll`: IMU roll from BNO085
- `imu_*`: Full IMU data from BNO085
- Mounting offset: Antenna line heading → vehicle heading correction
