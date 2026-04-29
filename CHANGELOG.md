# CHANGELOG

## NAV-NTRIP-001 — Produktiver NTRIP-Client auf generischem TCP-Transport

### NTRIP-Konfiguration
- **`ntrip_client_config_t`** Struktur: `mountpoint`, `username`, `password`, `user_agent`, `reconnect_initial_ms`, `reconnect_max_ms`, `response_timeout_ms`
- **`ntrip_client_config_set_defaults()`** setzt sichere Default-Werte
- Keine Secrets hart im Code — Konfiguration wird zur Laufzeit übergeben
- NVS-Vorbereitung: Struktur ist direkt aus NVS befüllbar

### State Machine produktiv
- **7 Zustände**: `IDLE → CONNECTING → SEND_REQUEST → WAIT_RESPONSE → STREAMING → ERROR → RETRY_WAIT`
- Altes Skeleton (simulierte Timeouts, `AUTHENTICATING`/`CONNECTED` States) vollständig ersetzt
- **Nonblocking** — alle Zustandswechsel in `service_step()`
- Disconnect-Erkennung in allen aktiven Zuständen

### HTTP/NTRIP-Request
- Generiert korrekten Request: `GET /<mountpoint> HTTP/1.0` + `User-Agent`, `Authorization: Basic`, `Ntrip-Version`, `Connection: close`
- **Base64-Encoding** für Basic Auth (inline, RFC 4648, keine externe Lib)
- Request wird über `transport_tcp.tx_write()` gesendet (kein direkter Socket-Zugriff)

### Response-Parsing
- Akzeptiert: `ICY 200 OK`, `HTTP/1.0 200`, `HTTP/1.1 200` → `STREAMING`
- Fehlerfälle: `401` (Unauthorized), `403` (Forbidden), `404` (Not Found) → `ERROR`
- Timeout-Erkennung: konfigurierbar via `response_timeout_ms`
- Daten nach HTTP-Headern (Inline-RTCM) werden direkt an `rtcm_buffer` weitergeleitet

### RTCM-Stream
- Im `STREAMING`-Zustand: `transport_tcp.rx_buffer` → `ntrip_client.rtcm_buffer`
- Keine RTCM-Decodierung, kein Routing, keine UART-Ausgabe im ntrip_client
- Keine RTCM-Daten in nicht-verbundenen Zuständen

### Reconnect / Backoff
- **Exponentieller Backoff**: `initial_ms` → ×2 → ... → `max_ms` (default: 1s → 2s → 4s → ... → 60s)
- Backoff wird bei erfolgreicher Verbindung zurückgesetzt
- `reconnect_count` wird pro Retry inkrementiert, bei `STREAMING` zurückgesetzt

### API-Änderungen (Breaking)
- `ntrip_client_init()`: Signatur unverändert (nur Zustand zurücksetzen)
- **NEU**: `ntrip_client_configure(client, config)` — setzt Konfiguration
- **NEU**: `ntrip_client_set_transport(client, tcp)` — setzt transport_tcp (ersetzt `set_tcp_source`)
- **NEU**: `ntrip_client_stop()` — Übergang zu IDLE
- **NEU**: `ntrip_client_get_last_error_code()` — HTTP-Status oder interner Fehlercode
- **ENTFALLEN**: `ntrip_client_set_tcp_source()` — ersetzt durch `set_transport`
- **ENTFALLEN**: `NTRIP_STATE_AUTHENTICATING`, `NTRIP_STATE_CONNECTED`, `NTRIP_STATE_RECONNECT` — ersetzt durch produktive States

### Neue Tests (37 Tests)
- Request-Generierung: Mountpoint, HTTP/1.0, User-Agent, Ntrip-Version, Connection: close
- Basic Auth: korrekte Base64-Kodierung, leeres Passwort, kein Auth ohne Username, Custom User-Agent
- State Transitions: alle 7 States, gültige/ungültige Übergänge, Start/Stop
- Response-Parsing: ICY 200 → STREAMING, HTTP/1.0 200 → STREAMING, HTTP/1.1 200 → STREAMING
- HTTP-Fehler: 401 → ERROR, 403 → ERROR, 404 → ERROR, Error → Retry
- RTCM-Forwarding: Bytes landen in rtcm_buffer, Inline-RTCM nach Headers, kein RTCM wenn nicht verbunden
- Backoff: Reconnect nach Error, exponentielles Verdoppeln, Cap bei max_backoff
- Timeout: connect_timeout → ERROR
- Fehlerfälle: kein Transport → ERROR, keine Config → ERROR, Disconnect im Streaming
- Edge Cases: NULL-Safety für alle API-Funktionen

### Test Results
- **37 NTRIP-Tests** + bestehende Tests = alle grün
- `pio test -e native` — 0 failures
- ADR-Checks: 4/4 grün

### Files Modified
| File | Change |
|------|--------|
| `components/ntrip_client/ntrip_client.h` | Komplett neu: Config-Struct, 7 States, transport_tcp Integration, neue API |
| `components/ntrip_client/ntrip_client.c` | Komplett neu: HTTP-Request-Builder, Base64, Response-Parser, produktive State Machine |
| `components/ntrip_client/CMakeLists.txt` | `PRIV_REQUIRES transport_tcp` hinzugefügt |
| `test/host/test_ntrip_client/test_ntrip_client.c` | Komplett neu: 37 Tests mit Fake TCP HAL |
| `test/sim/test_nav_chain/test_nav_chain.c` | Aktualisiert für neue ntrip_client API (transport_tcp statt raw ring buffer) |
| `docs/hardware/nav-ntrip.md` | **NEU** — Setup, Konfiguration, State Machine, Test-Verfahren, Fehlerfälle |

### Files NOT Changed (as required)
- Keine AOG-PGN-Änderungen
- Keine Steering-Änderungen
- Kein RTCM-Routing im ntrip_client
- Keine UART-Ausgabe im ntrip_client
- Keine Socket-I/O in task_fast
- transport_tcp enthält weiterhin keine NTRIP-Logik

---

## NAV-IO-001 Nacharbeit — UART/HAL Produktionshärtung

### UART Config Hardened
- **Full ESP32 UART parametrisation** via `uart_config_t` + `uart_param_config()`:
  - `baud_rate`, `data_bits`, `parity`, `stop_bits`, `flow_ctrl`, `source_clk`
  - Deterministic 8N1 default, UM980-suitable at 921600 baud
  - No hardware flow control (`UART_HW_FLOWCTRL_DISABLE`)
- ESP-IDF init sequence: `uart_param_config()` → `uart_driver_install()` → `uart_set_pin()`
- Type-safe mapping: `hal_uart_config_t` → ESP-IDF enums (`map_data_bits`, `map_stop_bits`, `map_parity`)
- Double-init prevention with `s_port_initialized[]` array

### TX Safety Fixed
- **Partial write protection**: `uart_write_bytes()` may write fewer bytes than requested
  - Old code: read full chunk from ring buffer → if HAL writes less → data lost
  - New code: read chunk → write to HAL → if partial → push unwritten bytes back to ring buffer
  - Breaks out of drain loop on backpressure (retry next `service_step` call)
- **HAL failure protection**: if `hal_uart_write()` returns ≤ 0, all bytes pushed back
- **Backpressure tracking**: `tx_backpressure_count` incremented on partial/failure events
- Byte order preserved across partial writes (single-task guarantee)

### Overflow Diagnostics Improved
- **RX highwater tracking**: `rx_highwater` records peak buffer usage
- **Extended diagnostics struct** (`transport_uart_diagnostics_t`):
  - `rx_total`, `tx_total`, `rx_overflows`, `rx_highwater`, `tx_backpressure_count`
  - `rx_available`, `tx_free` (current state snapshot)
- **Diagnostics API**: `transport_uart_get_diagnostics()` for monitoring/logging hooks
- Free buffer space reported in diagnostics for capacity planning

### Recovery Hooks Added
- **`transport_uart_reset()`**: flush HAL → clear RX/TX buffers → reset all counters
  - Port and baudrate config preserved
  - Safe to call at any time
- **`hal_uart_flush()`**: drain all pending RX data from HAL (read-discard loop with safety limit)
- **`hal_uart_port_reset()`**: deinitialize → reinitialize a port (full reinit path)
- Both work through the abstraction layer (no ESP-IDF headers in caller)

### Bug Fix
- `transport_uart_init()`: Fixed `sizeof(uart->tx_buffer)` → `sizeof(uart->tx_storage)` (was setting TX ring buffer capacity to 48 bytes instead of 512)

### New Tests (18 tests)
- `test_init_succeeds_for_console_port`
- `test_init_with_921600_baud`
- `test_init_null_params`
- `test_partial_write_no_data_loss`
- `test_partial_write_order_preserved`
- `test_buffer_integrity_after_partial_tx`
- `test_tx_backpressure_count`
- `test_tx_hal_failure_preserves_data`
- `test_rx_overflow_counter`
- `test_rx_highwater_tracking`
- `test_transport_uart_reset`
- `test_transport_uart_reset_null`
- `test_hal_uart_flush_drains_rx`
- `test_hal_uart_flush_not_initialized`
- `test_hal_uart_port_reset`
- `test_hal_uart_port_reset_null_config`
- `test_diagnostics_snapshot`
- `test_two_instances_independent`

### Test Results
- **84 tests pass**, 5 skipped (hardware smoke tests), 0 failures
- `pio test -e native` — all host and sim tests green

### Files Modified
| File | Change |
|------|--------|
| `components/hal_uart/hal_uart.h` | Added `hal_uart_port_reset()` declaration |
| `components/hal_uart/hal_uart.c` | Full ESP32 UART with `uart_config_t`, flush, port_reset, stubs for native |
| `components/hal_uart/CMakeLists.txt` | Added `PRIV_REQUIRES driver` |
| `components/transport_uart/transport_uart.h` | Extended diagnostics struct, reset API, highwater/backpressure fields |
| `components/transport_uart/transport_uart.c` | TX partial write safety, reset, diagnostics, sizeof bug fix |
| `test/host/test_transport_uart/test_transport_uart.c` | **NEW** — 18 tests |

### Files NOT Changed (as required)
- No API restructuring
- No GNSS parser extensions
- No Ethernet changes
- No NTRIP changes
- No `runtime_fast` modifications
- Layer structure preserved: HAL_UART → transport_uart → app_core
