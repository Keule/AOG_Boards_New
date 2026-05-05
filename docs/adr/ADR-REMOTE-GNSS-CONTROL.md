# ADR: Remote GNSS Command Interface + UM980 Control Layer

**Task:** NAV-REMOTE-GNSS-CMD-001
**Date:** 2025-05-04
**Status:** Accepted

## Context

The AOG ESP Multiboard firmware has two UM980 GNSS receivers connected via UART.
Previously, the only way to query receiver configuration was the boot-time snapshot
(NAV-UM980-CONFIG-SNAPSHOT-001) which ran BEFORE the runtime loop started.

Problems with the boot-only approach:
1. Configuration can change during runtime (e.g., NTRIP reconnects, RTCM data)
2. No way to diagnose receiver state after boot without reflashing
3. No mechanism for ad-hoc commands (e.g., query fix mode, signal strength)
4. Boot snapshot cannot use runtime UART service (no `transport_uart_service_step`)

## Decision

Create a new `gnss_um980_control` component implementing a SparkFun-style
command/response layer with:

1. **Exclusive UART access** via mutex
2. **NMEA parser suspension** during commands (set `rx_source = NULL`)
3. **One-shot pump** (`transport_uart_pump`) for HAL↔ringbuffer bridging
4. **Safety blocklist** preventing dangerous commands (FRESET, UPGRADE)
5. **Automatic CRLF** appending and URL decoding for HTTP integration
6. **Retry logic** (up to 2 retries on timeout)

### HTTP Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/gnss/1/send/<cmd>` | GET | Send arbitrary command to receiver 1 |
| `/gnss/2/send/<cmd>` | GET | Send arbitrary command to receiver 2 |
| `/gnss/1/config` | GET | Live query: VERSIONA, CONFIG, MODE, MASK |
| `/gnss/2/config` | GET | Live query: VERSIONA, CONFIG, MODE, MASK |
| `/gnss/config` | GET | Live query both receivers |
| `/gnss/1/status` | GET | Control layer statistics |
| `/gnss/2/status` | GET | Control layer statistics |
| `/gnss/1/unlogall` | GET | Send UNLOGALL to receiver 1 |
| `/gnss/2/unlogall` | GET | Send UNLOGALL to receiver 2 |

### UART Ownership Model

```
1. Acquire mutex (5s timeout)
2. Save gnss->rx_source
3. Set gnss->rx_source = NULL (suspend NMEA parser)
4. Flush RX ring buffer (drain stale data)
5. Send command via transport_uart_tx_write() + pump()
6. Wait 50ms settling time
7. Collect response via pump() + rx_read() with timeout/quiet detection
8. Restore gnss->rx_source
9. Flush stale data from command
10. Release mutex
```

### Snapshot Refactoring

The boot-time snapshot (`gnss_um980_snapshot`) was refactored to use the control
layer instead of direct transport_uart access. This eliminates duplicate UART
pump code and ensures consistent behavior.

## Consequences

### Positive
- Runtime diagnostics: query receivers at any time via HTTP
- Consistent UART access pattern (mutex + parser suspension)
- Snapshot boot code simplified (delegates to control layer)
- Safety blocklist prevents accidental destructive commands
- All commands are logged via ESP_LOGI (captured by remote_log)

### Negative
- Commands block the HTTP handler thread (Core 0) for up to `timeout * retries`
  — mitigated by 2s default timeout and max 2 retries (max ~6s block)
- `UNLOGALL` disables ALL NMEA logging until manually restored
  — mitigated by not exposing UNLOGALL in the config query path

### Risks
- If mutex is held too long, other HTTP requests queue up
- Parser suspension means NMEA data is lost during command execution
  — acceptable for diagnostic queries (short duration)

## Build Impact

- **RAM:** 40.5% (unchanged from previous build — control instances are static)
- **Flash:** 16.6% (+5KB from control component + HTTP handlers)
- **New component:** `gnss_um980_control/` (617 lines)
- **Modified:** `remote_diag.c` (+280 lines), `gnss_um980_snapshot.c` (rewritten)
- **Total HTTP endpoints:** 15 (up from 14)
