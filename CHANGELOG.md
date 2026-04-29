# CHANGELOG

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
