# NAV-UM980-CONFIG-SNAPSHOT-001 — UM980 Boot-Time Config Snapshot

**Task:** AOG_Boards_New-015
**Component:** `gnss_um980_snapshot/`
**Date:** 2025-05-04
**Status:** Implemented, built successfully

---

## Purpose

Capture UM980 dual-receiver configuration at boot time, **before any runtime changes** (NTRIP connect, RTCM forwarding, mode switches). This provides a reliable audit trail of what each receiver was configured with at power-on.

The snapshot is read-only — no commands are sent that could alter receiver state.

---

## Boot Sequence

The config snapshot runs early in the boot process, integrated into `app_core.c`:

```
Power-on
  |
  v
UART transport init (both receivers)
  |
  v
gnss_um980_snapshot_init()  — zero buffers, reset state
  |
  v
gnss_um980_snapshot_run_all(uart_primary, uart_secondary)
  |
  +-- Wait 2000 ms (receiver settle after power-on)
  |
  +-- Query RX1: version → config → mode → mask
  |     (flush RX before each command, 300 ms spacing, 1500 ms timeout)
  |
  +-- Query RX2: version → config → mode → mask
  |     (same timing)
  |
  v
Build combined snapshot string
  |
  v
Normal boot continues (NTRIP, RTCM, GNSS processing)
```

**Total blocking time: ~10 seconds** (2s settle + ~4s per receiver).

On failure or timeout, boot continues normally — the snapshot is non-fatal.

---

## Commands Used (All READ-ONLY)

| # | Command | Description |
|---|---------|-------------|
| 1 | `version\r\n` | Firmware version string |
| 2 | `config\r\n` | Current receiver configuration |
| 3 | `mode\r\n` | Operating mode |
| 4 | `mask\r\n` | Signal/tracking mask settings |

### Forbidden Commands (NEVER sent)

The following commands are explicitly excluded to prevent accidental configuration changes:

- `unlog` — would disable data streams
- `saveconfig` — would write to flash
- `freset` — would factory reset the receiver
- `mode <mode>` — would change operating mode
- Any command with parameters that modify state

---

## Timing Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| `GNSS_SNAPSHOT_SETTLE_MS` | 2000 ms | Wait after UART init for receiver boot |
| `GNSS_SNAPSHOT_CMD_SPACING_MS` | 300 ms | Delay between consecutive commands |
| `GNSS_SNAPSHOT_RESP_TIMEOUT_MS` | 1500 ms | Hard timeout per command response |
| `GNSS_SNAPSHOT_BUFFER_SIZE` | 16384 (16 KB) | Buffer per receiver |
| **Total blocking time** | **~10 s** | 2s settle + 2×(4×300ms spacing + 4×1500ms worst-case) |

Response end detection: after receiving at least one byte, the reader waits for 300 ms of silence (no new UART data) before considering the response complete.

---

## Memory Layout

| Buffer | Size | Description |
|--------|------|-------------|
| `s_snapshot[0]` | 16 KB | Receiver 1 raw ASCII (version+config+mode+mask) |
| `s_snapshot[1]` | 16 KB | Receiver 2 raw ASCII |
| `s_combined[]` | ~32.5 KB | Combined view (RX1 + separator + RX2), built lazily |
| **Total static RAM** | **~64.5 KB** | All statically allocated at compile time |

All buffers are statically allocated — no heap allocation. Data is written once at boot and never modified.

---

## HTTP Endpoints

All endpoints are served from the existing `remote_diag` HTTP server (port 80).

### `GET /gnss/config_snapshot`

Combined snapshot from both receivers.

```
Content-Type: text/plain
```

**Response format:**
```
===== UM980 RECEIVER 1 SNAPSHOT =====
> version
<UM980 firmware response>
> config
<UM980 configuration response>
> mode
<UM980 mode response>
> mask
<UM980 mask response>
===== END =====

========================================

===== UM980 RECEIVER 2 SNAPSHOT =====
> version
<UM980 firmware response>
...
===== END =====
```

### `GET /gnss/1/config_snapshot`

Receiver 1 snapshot only (same format as above, single receiver).

### `GET /gnss/2/config_snapshot`

Receiver 2 snapshot only (same format, single receiver).

### `GET /gnss/config_status`

JSON status of the snapshot operation:

```json
{
  "rx1_complete": true,
  "rx2_complete": true,
  "rx1_timeout": false,
  "rx2_timeout": false,
  "rx1_bytes": 1247,
  "rx2_bytes": 1198,
  "duration_ms": 10234
}
```

| Field | Type | Description |
|-------|------|-------------|
| `rx1_complete` | bool | All 4 commands succeeded on RX1 |
| `rx2_complete` | bool | All 4 commands succeeded on RX2 |
| `rx1_timeout` | bool | Any command timed out on RX1 |
| `rx2_timeout` | bool | Any command timed out on RX2 |
| `rx1_bytes` | uint | Total bytes captured from RX1 |
| `rx2_bytes` | uint | Total bytes captured from RX2 |
| `duration_ms` | uint | Total time for complete snapshot |

---

## Safety Rules

1. **Read-only commands only** — no `unlog`, `saveconfig`, `freset`, or mode changes
2. **No RTCM forwarding during snapshot** — snapshot completes before RTCM router starts
3. **No NTRIP connect during snapshot** — snapshot completes before NTRIP client starts
4. **Non-fatal on failure** — timeout or error does not prevent normal boot
5. **No heap allocation** — all buffers are static, no malloc/free at runtime
6. **UART flush before each command** — prevents stale data contamination
7. **Single-run** — snapshot runs once at boot, data is immutable afterward

---

## Test Procedure

### Prerequisites
- Board powered on with both UM980 receivers connected
- Ethernet connected (for HTTP access)
- Serial monitor connected (for boot log observation)

### 1. Observe Boot Log

Watch serial output for snapshot messages:

```
I (3200) SNAPSHOT: UM980 Config Snapshot starting — receiver settle delay 2000 ms
I (5204) SNAPSHOT: Querying UM980 Receiver 1 (UART primary)...
I (10234) SNAPSHOT: Receiver 1: OK (1247 bytes)
I (10234) SNAPSHOT: Querying UM980 Receiver 2 (UART secondary)...
I (15267) SNAPSHOT: Receiver 2: OK (1198 bytes)
I (15267) SNAPSHOT: Config Snapshot complete in 10234 ms — RX1: OK (1247 B), RX2: OK (1198 B)
```

### 2. Verify Combined Snapshot

```bash
curl http://<BOARD_IP>/gnss/config_snapshot
```

Expected: multi-line text with `===== UM980 RECEIVER N SNAPSHOT =====` headers, version/config/mode/mask responses with `>` command prefixes.

### 3. Verify Individual Receivers

```bash
curl http://<BOARD_IP>/gnss/1/config_snapshot
curl http://<BOARD_IP>/gnss/2/config_snapshot
```

Each should contain exactly one receiver's data.

### 4. Check Status

```bash
curl http://<BOARD_IP>/gnss/config_status
```

Expected: JSON with `rx1_complete: true`, `rx2_complete: true`, byte counts > 0, `duration_ms` between 8000–12000.

### 5. Edge Case: Receiver Disconnected

Power off one receiver, reboot, check:
- Status shows the disconnected receiver as `complete: false`, `timeout: true`
- The connected receiver still shows full data
- Boot completes normally (no hang)

---

## Implementation Files

| File | Description |
|------|-------------|
| `components/gnss_um980_snapshot/gnss_um980_snapshot.c` | Core implementation (snapshot logic, UART I/O) |
| `components/gnss_um980_snapshot/include/gnss_um980_snapshot.h` | Public API, timing constants, command table |
| `components/gnss_um980_snapshot/CMakeLists.txt` | Component build definition |
| `components/remote_diag/remote_diag.c` | HTTP endpoint handlers |
| `components/remote_diag/remote_diag.h` | Endpoint documentation |
| `components/remote_diag/CMakeLists.txt` | Added `gnss_um980_snapshot` dependency |
| `components/app_core/app_core.c` | Boot integration (init + run_all call) |

---

## Build Results

```
RAM:   40.5% (132,616 bytes)
Flash: 16.4% (515,300 bytes)
SUCCESS — 0 errors, 0 warnings
```

---

## Known Limits

1. **Blocks boot for ~10 seconds** — this is by design (must capture before runtime starts). Could be made async in a future version.

2. **No retry on timeout** — if a command times out, the snapshot moves to the next command. A retry mechanism could improve reliability but would increase boot time.

3. **Static allocation** — 64.5 KB of RAM is permanently reserved even after boot. On RAM-constrained boards this could be moved to heap with a "snapshot done → release buffer" pattern.

4. **Single snapshot per boot** — the data is captured once and never updated. If a receiver is reconfigured at runtime, the snapshot will not reflect those changes.

5. **No structured parsing** — responses are stored as raw ASCII text. No attempt is made to parse version numbers, config keys, etc. into structured fields.

6. **UART only** — snapshot uses the transport_uart API. If the receiver were connected via another transport (SPI, I2C), this component would need modification.

---

## Integration Notes

- Called from `app_core.c` after `transport_uart_init()` and before NTRIP/RTCM startup
- Depends on `transport_uart` (for UART read/write), `log`, `freertos`
- HTTP endpoints registered in `remote_diag.c` via `gnss_um980_snapshot.h` include
- Type-erased UART API (`void*` pointers) avoids circular header dependencies
