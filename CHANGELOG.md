# Changelog

All notable changes to this project will be documented in this file.

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
