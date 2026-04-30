# Changelog

All notable changes to this project will be documented in this file.

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
