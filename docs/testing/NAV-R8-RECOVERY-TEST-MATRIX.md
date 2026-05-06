# NAV-R8 Recovery Test Matrix

## Ziel
Validierung eines sauberen Neuaufbaus vom Mitternachtsstand mit allen Erkenntnissen vom 2026-05-05.

## Build
- `pio run -e nav_esp32_t_eth_lite`
- `pio test -e native`

## Boot / RemoteDiag / OTA
Akzeptanzlog:
```text
BOARD_PROFILE: profile=lilygo_t_eth_lite_esp32 role=NAV
BOARD_PROFILE: BOARD PIN SANITY CHECK: PASS
HAL_ETH: ETH: got_ip=...
REMOTE_DIAG: endpoints registered: /status,/diag,/version,/logs/raw,/logs/view,/logs,/favicon.ico,/ota,/reboot
```

HTTP Checks:
- `GET /status` -> 200
- `GET /diag` -> 200
- `GET /logs/raw` -> 200
- `GET /logs/view` -> 200
- `GET /logs` -> 200 or redirect
- `GET /ota` -> 200
- `POST /ota` works and next boot partition changes.

## NTRIP / RTCM
Akzeptanzlog:
```text
NTRIP_CFG: source=local_secrets host=set port=2101 mount=set user=set password=set
NTRIP: eth_ready=1 config_ready=1 state=connected rx_bytes increasing
RTCM_ROUTER: in_bytes increasing out1_bytes increasing out2_bytes increasing drops=0
GNSS_RTCM_TX: primary tx_bytes increasing drops=0
GNSS_RTCM_TX: secondary tx_bytes increasing drops=0
```

## UART Stability
Akzeptanz:
```text
GNSS_UART primary rx_overruns=0 rx_ring not permanently full
GNSS_UART secondary rx_overruns=0 rx_ring not permanently full
GNSS_BAD_CSUM not continuously increasing at high rate
```

## UM980 NMEA Profile
Target profile:
```text
GGA 10 Hz
RMC 10 Hz
GSV 0 Hz
GST disabled unless explicitly enabled
GSA epoch 1 Hz; sentence-rate may be 3 Hz if multi-talker
```

RX2 expected:
```text
GNSS_CFG2 state=DONE port=COM2
gga 8..12 rmc 8..12 gsv 0 gst 0 if disabled gsa OK_MULTI_TALKER if 3 sentence/s
```

RX1 expected after fix:
```text
GNSS_CFG rx1 state=DONE or explicit FAILED with tested COM matrix
selected_port=COM1 or COM2 based on ACK/effect
gga 8..12 rmc 8..12 gsv 0
```

## Snapshot Validity
Required diagnostic fields:
```text
GNSS_VALIDITY primary: last_gga_seen_age last_gga_valid_age gga_fix_quality last_rmc_seen_age last_rmc_valid_age rmc_status position_valid motion_valid fresh drop_reason
GNSS_VALIDITY secondary: ...
```
No raw `4294967295` ages in human-facing logs; use `invalid/none` plus reason.

## AOG / UDP
Only after GNSS valid/fresh:
```text
AOG_NAV state=OK or explicit drop reason
UDP peer_seen may remain 0 until AgIO is active; do not treat as GNSS blocker.
```
