# Changelog

All notable changes to this project will be documented in this file.

## [NAV-FIX-001-R2] — AOG-NAV auf echten 100-Hz Fast-Pfad umgestellt

### Changed
- **aog_navigation_app.c/h**: AOG-NAV Fachlogik komplett in Fast-Path-Hooks umgezogen
  - Neue 3-Phase Hooks: `fast_input` (RX Parsing), `fast_process` (Snapshot + Gating + PGN214), `fast_output` (No-op/Platzhalter)
  - `service_step` auf NULL gesetzt — keine Core-0 Fachlogik mehr
  - `AOG_SEND_INTERVAL_MS 50` entfernt — PGN214 wird jeden Fast-Tick ausgegeben (100 Hz / 10 ms)
  - `aog_send_interval_ms` Feld aus `aog_nav_app_t` entfernt
  - Legacy `service_step()` bleibt als Test-Wrapper (delegiert an `fast_process`)
- **app_core.c**: `s_nav_app` Service-Group von `SERVICE_GROUP_DIAGNOSTICS` auf `SERVICE_GROUP_UDP` geaendert (service_step=NULL, Gruppe nur fuer Zaehlung)
- **task_fast.c**: `ctx.timestamp_us` und `ctx.period_us` werden VOR den Fast-Hooks gesetzt (war: nach)
- **test_aog_pgn214.c**: `test_cyclic_20hz` ersetzt durch `test_cyclic_100hz`
  - Test prueft: PGN214 wird bei JEDEM Fast-Tick ausgegeben (0, 5ms, 9ms, 10ms, 20ms → 5 Frames)
- **docs/hardware/nav-aog.md**: PGN 214 Trigger aktualisiert auf "Periodic, 100 Hz (10 ms)"

### Removed
- `#define AOG_SEND_INTERVAL_MS 50 /* 20 Hz */` — keine 20-Hz/50-ms Konstante mehr
- `app->aog_send_interval_ms` Feld und Intervall-Pruefung im PGN214 Ausgabepfad

## [STEER-MIG-001] — Steering-Komponenten in ESP-IDF Component-Modell migriert

### Changed
- **steering_control.h/c**: vollstaendig neu geschrieben — Fast-Path-Hooks (fast_input/fast_process/fast_output), PID-Regler mit Anti-Windup, Safety-Integration, Diagnostics-Snapshot
- **was_sensor.h/c**: erweitert mit Freshness-Tracking, Plausibilitaetspruefung, erweitertes Snapshot (was_sensor_data_t), Sprung-Rate-Erkennung, Voltage-Berechnung
- **steering_control/CMakeLists.txt**: neue Dependencies (steering_safety, steering_output, runtime_types)
- **was_sensor/CMakeLists.txt**: neue Dependency (runtime_types)

### Added
- **steering_safety** (NEU): Reine-Logik Safety-Gate mit 10 Pflichtbedingungen
  - `steering_safety_evaluate()`: bewertet alle Safety-Bedingungen pro Zyklus
  - 12 Reason-Codes (OK + 11 Fehlerzustände)
  - Konfigurierbare Timeouts (Command 200ms, Sensor 200ms, Comms 500ms)
  - Setpoint-Clamping bei OOR (Entscheidung: Clamp, nicht Motor aus)
  - Statistik-Tracking (eval_count, unsafe_count, reason_counts[])
- **steering_output** (NEU): HAL-abstrahierte Motor/PWM-Ausgabe
  - `steering_output_hal_t`: mockbares HAL-Interface (set_enable/set_direction/set_pwm/emergency_stop)
  - Deadzone (5%), Saturation (-1.0..+1.0), NaN/Inf-Guard
  - `steering_output_mock_hal_init()` fuer Host-Tests
- **steering_diagnostics** (NEU): Fehlerhistorie und Health-Status
  - 16-Eintraege Fehlerhistorie (Ring-Buffer)
  - Safety-Uebergaenge werden automatisch protokolliert
  - Diagnostics-Snapshot fuer externe Abfrage
- **test_steering_safety** (19 Tests): alle 10 Safety-Bedingungen, Boundaries, Statistiken
- **test_steering_output** (12 Tests): Safety-Block, Richtung, Deadzone, Saturation, NaN/Inf
- **test_steering_sensor** (10 Tests): Calibration, Span-Zero, Voltage, Plausibilitaet
- **test_steering_control** (12 Tests): Default, Enable, PID, Anti-Windup, NaN, Diagnostics
- **docs/architecture/steering.md**: vollstaendige Steering-Architektur-Dokumentation
- **docs/hardware/steering.md**: Hardware-Interface, Safety-Matrix, CMake-Dependencies

### Technical Details

#### AP-A: Komponentenstruktur
- 6 Rollen klar getrennt: command, sensor, safety, control, output, diagnostics
- Alle Komponenten nutzen runtime_component_t Modell
- Hardware-Zugriffe nur ueber HAL-Abstraktionen

#### AP-B: Steering Fast-Pfad
- fast_input: Command + Sensor Snapshots lesen
- fast_process: Safety-Gate + PID-Berechnung
- fast_output: Motor-Ausgabe + Diagnostics
- Keine blockierenden Aufrufe, kein Logging-Spam

#### AP-C: Safety-Gating
- 10 Pflichtfaelle implementiert und getestet
- Default nach Init: Motor aus (global_enabled=false)
- Setpoint OOR → Clamp ( dokumentiert, nicht Motor aus)
- Alle Reason-Codes als String abbildbar

#### AP-D: Sensor-Skalierung
- ADC→Voltage→Winkel Pipeline
- Plausibilitaet: ±40° Abs-Max, 5°/10ms Sprung-Rate
- Freshness: 200ms Default-Timeout

#### AP-E: Motor/PWM-Abstraktion
- HAL-Interface mit 4 Funktionen
- Mock-HAL fuer Tests
- PWM Deadzone + Saturation
- NaN/Inf Guard

### Verification
- ⚠️ No PlatformIO in sandbox — code review only
- ✔ steering_safety.c: alle 10 Safety-Bedingungen als reine Logik implementiert
- ✔ steering_output.c: Deadzone, Saturation, NaN-Guard verifiziert
- ✔ was_sensor.c: Freshness, Plausibilitaet, Voltage-Berechnung korrekt
- ✔ steering_control.c: Fast-Path 3-Phasen, PID mit Anti-Windup
- ✔ 53 Host-Tests definiert (safety=19, output=12, sensor=10, control=12)
- ✔ Keine Arduino-Abhaengigkeit
- ✔ Keine NAV-Architektur-Aenderungen

### Known Limitations
- Kein IMU in Steering-Pfad (TODO: FULL-INTEGRATION-001)
- PID-Parameter hardcoded (TODO: NVS)
- Kein echte HAL GPIO/PWM (Skeleton)
- Kein Autodetect fuer WAS-Kalibrierung

## [NAV-FIX-001] - PGN-Format, 100-Hz Fast Path, Task-Timing-Härtung

### Changed
- **aog_frame.h**: binding format documentation updated to 16-bit PGN (ADR-0006), CRC STRICT/TOLERANT modes documented
- **aog_frame.c**: encoder uses 2-byte LE PGN (`[SRC][PGN_lo][PGN_hi]`), CRC = sum(bytes[2..5+len]) mod 256
- **aog_pgn.h**: all PGN constants as uint16_t, scan_reply module_type field added (0x78 = GPS)
- **aog_navigation_app.h**: `fast_process` hook declared, 3-state heading model (VALID/STALE/INVALID), 8-state output model
- **aog_navigation_app.c**: `aog_nav_app_fast_process()` implemented, registered as `component.fast_process` for 100-Hz Core 1 integration
- **task_fast.c**: replaced `vTaskDelay()` with `vTaskDelayUntil()` for deterministic 100-Hz (10ms period), added deadline-miss detection
- **runtime_stats.h/c**: added `runtime_stats_record_deadline_miss()` / `runtime_stats_get_deadline_miss_count()` API
- **docs/architecture/runtime.md**: updated with vTaskDelayUntil timing model and deadline-miss documentation
- **test/host/test_aog_pgn214/test_aog_pgn214.c**: 49 tests (8 new golden byte regression vectors), all golden frames use 16-bit PGN format

### Added
- **ADR-0006**: PGN Frameformat-Entscheidung — 16-bit PGN binding (AOG v5), not 1-byte
- **`aog_nav_app_fast_process()`**: delegates to `service_step` with fast cycle context (Core 1, deterministic 100 Hz)
- **Deadline-miss counter** in runtime_stats — incremented when `vTaskDelayUntil` returns late
- **10 golden byte regression tests** (tests 40-49): header bytes, sentinel payload, discovery CRC, hello/scan frames, heading encoding, fix quality byte, complete PGN 214 frame
- **CHANGELOG.md**: NAV-FIX-001 entry (this document)
- **test_results.txt**: NAV-FIX-001 test evidence section

### Technical Details

#### AP-A: PGN Format Decision
- Frame format: `[0x80][0x81][SRC][PGN_lo][PGN_hi][LEN][DATA...][CRC]`
- Total frame size = 7 + data_length (not 6 + data_length as with 1-byte PGN)
- PGN encoded as uint16_t, 2 bytes little-endian
- CRC = sum(SRC + PGN_lo + PGN_hi + LEN + DATA[0..n-1]) mod 256
- Documented in ADR-0006

#### AP-B: 100-Hz Fast Path Integration
- `aog_navigation_app` registers `fast_process` hook in `aog_nav_app_init()`
- `aog_nav_app_fast_process()` delegates to `aog_nav_app_service_step()` with timestamp from `fast_cycle_context_t`
- Core 0 = transport (service_step), Core 1 = domain logic (fast_process)
- No change to service_step logic — same output gating, same PGN 214 encoding

#### AP-C: vTaskDelayUntil Härtung
- `vTaskDelay()` replaced with `vTaskDelayUntil()` — eliminates cumulative jitter
- Each cycle starts at a fixed 10ms interval from the previous cycle START
- Deadline-miss detected: `expected_wake = last_wake + period`, compare with `actual_wake`
- Miss counter exposed via `runtime_stats_get_deadline_miss_count()`

### Verification
- ⚠️ No PlatformIO in sandbox — code review only
- ✔ `aog_frame.c`: CRC calculation matches spec (sum mod 256 over bytes[2..end-1])
- ✔ `aog_pgn.c`: 16-bit PGN decode roundtrip correct
- ✔ `task_fast.c`: vTaskDelayUntil pattern verified against FreeRTOS docs
- ✔ `runtime_stats.c`: deadline-miss counter initialized to 0, incremented atomically
- ✔ `test_aog_pgn214.c`: 49 tests defined (all categories covered)
- ✔ ADR-0006: ACCEPTED, consistent with existing implementation
- ✔ All 4 ADR compliance checks: no new violations

## [NAV-RTCM-001] - Produktives RTCM-Routing (NTRIP → Router → UM980)

### Changed
- **rtcm_router.h**: added `output_overflow_count` field to `rtcm_router_t`
- **rtcm_router.c**: `service_step` now increments `output_overflow_count` when any output buffer is full
- **rtcm_router.h**: added `rtcm_router_get_output_overflow_count()` API
- **transport_uart.h**: added `tx_errors` and `tx_overflows` to `transport_uart_stats_t`
- **transport_uart.c**: `service_step` now tracks `tx_errors` when `hal_uart_write()` returns negative; `tx_write` tracks `tx_overflows` from ring buffer overflow
- **transport_uart.h**: `tx_pushback_bytes` marked DEPRECATED (no longer incremented; peek/consume pattern)
- **test_transport_uart.c**: updated partial write tests for peek/consume pattern; added 3 new tests for tx_errors and tx_overflows
- **test/host/mocks/board_profile_mock.c**: added `<stddef.h>` include for NULL

### Added
- **`rtcm_router_get_output_overflow_count()`**: returns number of output overflow events
- **`transport_uart_stats_t.tx_errors`**: counts HAL write errors
- **`transport_uart_stats_t.tx_overflows`**: counts TX ring buffer overflow events
- **6 new tests in test_rtcm_router** (identical bytes, primary-full-isolation, overflow-count, null-safety, generic-no-hal, multi-step-accumulate)
- **3 new tests in test_transport_uart** (tx_errors, tx_overflows, new-fields-zero)
- **5 new end-to-end tests in test_nav_chain** (chain-wiring, full-flow, multi-message, service-order, no-hal-dependency)
- **docs/hardware/nav-rtcm-routing.md**: complete documentation for RTCM routing data flow, stats, test plan, error handling

### Verification
- ✔ 21/21 rtcm_router host tests PASS (gcc native)
- ✔ 25/25 transport_uart host tests PASS (gcc native)
- ✔ 5/5 nav_chain sim tests PASS (gcc native)
- ✔ 4/4 ADR compliance checks PASS
- ✔ Productive wiring verified in app_core.c (lines 178-181)
- ✔ Service chain: ntrip_client → rtcm_router → transport_uart (no monolith)

## [Nacharbeit Review-Fixes] - Work/Config Service Profiles + runtime_set_system_mode

### Changed
- **runtime.h**: removed SKELETON marker; runtime_set_system_mode() is now productive
- **runtime.c**: complete rewrite — uses runtime_mode for pure-logic mode switching, unified service_task_fn (no more 4 copy-paste functions), tasks read live profiles from s_profiles[] every iteration
- **runtime_types.h**: removed SKELETON markers from system_mode_t and service_profile_t docs
- **docs/architecture/runtime.md**: updated Modi section — productive vs TODO clearly separated, no hard MUSS claims for unimplemented features
- **docs/adr/ADR-0002-runtime-modell.md**: added Umsetzungsstand section, changed MÜSSEN to SOLLEN for unimplemented features
- **tools/adr_checks/adr_rules.yaml**: all include patterns now use file extension filters (*.c, *.h) instead of bare **
- **tools/adr_checks/check_all.py**: added file extension filter, binary file detection, max-files-per-rule safety limit (500)

### Added
- **runtime_mode.h / runtime_mode.c**: host-testable pure-logic mode/profile API (no ESP-IDF dependency)
  - runtime_mode_init() — sets mode to WORK, applies work profiles
  - runtime_mode_set(mode) — validates mode, applies work/config profiles
  - runtime_mode_get() — returns current mode
  - runtime_mode_get_profile(group) — returns live profile pointer
  - runtime_mode_work_profile(group) / runtime_mode_config_profile(group) — const profile accessors
- **Work-mode profiles**: UART/UDP/TCP_NTRIP priority=5, period=10ms; Diagnostics priority=3, period=100ms
- **Config-mode profiles**: UART/UDP/TCP_NTRIP priority=5, period=10ms; Diagnostics priority=6, period=50ms
- **runtime_get_system_mode()** API to query current mode
- **14 new host tests** in test/host/test_runtime_mode/test_runtime_mode.c

### Fixed
- **runtime_set_system_mode() was a no-op**: now fully implemented with validation, profile application, and mode storage
- **runtime_get_service_profile() was unimplemented**: now delegates to runtime_mode_get_profile()
- **Service tasks read cached profile copies**: tasks now read live s_profiles[] every loop iteration, so period/suspended changes take effect immediately
- **Four identical copy-paste task functions**: replaced with single parameterized service_task_fn()
- **ADR checker glob patterns without extensions**: now filtered to *.c/*.h only, prevents scanning build artifacts

### TODO (next step, documented)
- vTaskPrioritySet() for live priority changes without task restart (task handles need to be stored)
- Explicit Suspend/Resume API for individual service groups
- Feature activation per mode

## [NAV-NTRIP-001 Nacharbeit] - NTRIP Client Hardening

### Changed
- **app_core API wiring**: replaced `ntrip_client_set_tcp_source()` with `ntrip_client_configure()` + `ntrip_client_set_transport()` + guarded `ntrip_client_start()`
- **ntrip_client state machine**: new states BUILD_REQUEST, SEND_REQUEST, WAIT_RESPONSE, RETRY_WAIT replace old AUTHENTICATING/RECONNECT skeleton states
- **transport_tcp**: added TX ring buffer with `transport_tcp_tx_write()`, `transport_tcp_tx_available()`, `transport_tcp_tx_free()`. Service step now drains TX buffer.
- **ntrip_client CMakeLists.txt**: added `transport_tcp` dependency

### Added
- **`ntrip_client_config_t`**: host, port, mountpoint, username, password, user_agent, timeout_ms, reconnect_backoff_ms
- **`NTRIP_CLIENT_CONFIG_DEFAULT()`** macro for safe initialization
- **`ntrip_client_configure()`**: sets config with validation (host + mountpoint required)
- **`ntrip_client_set_transport()`**: sets transport_tcp_t reference
- **`ntrip_client_get_last_error()`**: returns ntrip_err_t from last failure
- **`ntrip_client_get_http_status()`**: returns parsed HTTP status code
- **Request partial-write safety**: `request_sent_offset` tracks progress, only transitions to WAIT_RESPONSE when fully sent
- **Bounds-safe request builder**: `request_builder_t` with `rb_append()`/`rb_printf()` helpers, overflow → NTRIP_ERR_REQUEST_TOO_LARGE
- **Inline base64 encoder**: for HTTP Basic Auth credentials
- **HTTP status parsing**: detects 200 (OK), 401 (auth failed), 403 (forbidden), 404 (not found)
- **Error path**: HTTP errors, timeout, disconnect → state ERROR → transport disconnect → RETRY_WAIT (backoff)
- **Retry logic**: reconnect_backoff_ms timer in RETRY_WAIT state, fresh request_offset=0 on retry
- **35 tests in test_ntrip_client**: config validation, start guards, state transitions, partial write, bounds safety, HTTP error codes, timeout, retry, reconnect counter, null safety

### Fixed
- **NTRIP starts without config**: start() now requires valid config AND transport
- **Request data loss on partial write**: request_sent_offset ensures no bytes are skipped
- **Request buffer overflow**: bounds-checked builder returns NTRIP_ERR_REQUEST_TOO_LARGE
- **Error state cleanup**: ERROR state disconnects transport and clears RTCM buffer
- **base64 buffer undersize**: cred_b64 buffer now large enough for max-length credentials

## [NAV-IO-001b] - HAL-UART Platform Split + UART Hardening

### Changed
- **hal_uart split into platform-neutral core + ESP32 implementation + stub**
  - `hal_uart.h` is now fully platform-neutral (no ESP-IDF includes)
  - `hal_uart.c` contains only ops dispatch logic
  - `hal_uart_esp32.c` provides real ESP-IDF UART driver (uart_param_config, uart_set_pin, uart_driver_install)
  - `hal_uart_stub.c` provides no-op stubs for native/host builds
  - CMakeLists.txt conditionally selects ESP32 vs stub source based on `ESP_PLATFORM`

### Added
- **Extended hal_uart_config_t** with pin config (tx_pin, rx_pin) and buffer sizes (rx_buffer_size, tx_buffer_size)
- **New HAL UART API**: `hal_uart_flush()`, `hal_uart_reset()`, `hal_uart_available()`
- **`hal_uart_stub_ops()`** provider for native test injection
- **TX partial write safety** in transport_uart: unwritten bytes are pushed back to ring buffer on backpressure
- **RX overflow diagnostics**: overflow counter propagated from ring buffer to transport_uart stats
- **`transport_uart_stats_t`**: tracks rx_bytes_in, rx_overflow_count, tx_bytes_out, tx_partial_writes, tx_pushback_bytes
- **`transport_uart_diagnostics_t`**: buffer sizes, usage, overflow totals
- **`transport_uart_reset()`**: re-initialises buffers and stats without re-initialising HAL port
- **`transport_uart_get_stats()`** and **`transport_uart_diagnostics()`** API
- **Native test infrastructure**: `extra_scripts/native_test.py` for include path and source injection
- **Board profile mock** (`test/host/mocks/board_profile_mock.c`) for native tests (no ESP-IDF dependency)
- **28 new tests** in `test/host/test_hal_uart/` (ops dispatch, stub provider, flush, reset, available, null safety)
- **23 new tests** in `test/host/test_transport_uart/` (partial write, overflow, stats, reset, diagnostics, null safety)

### Fixed
- **TX partial write data loss**: transport_uart now pushes unwritten bytes back to the TX ring buffer instead of silently dropping them
- **Missing native test build**: `[env:native]` now has `extra_scripts` for proper component compilation
