# Fast-Loop Diagnostics (NAV-ETH-BRINGUP-001-R2 WP-E)

## Overview

The fast loop (`task_fast`) runs on **Core 1** at a target frequency of
**100 Hz** (10 ms period). It calls all registered `fast_input`,
`fast_process`, and `fast_output` callbacks deterministically using
`vTaskDelayUntil()`.

## Diagnostic Output Format

```
RUNTIME_FAST: hz=99.80 report_cycles=500 total_cycles=2534
              missed=0 processing_us=56 remaining_us=9944
              period_avg_us=10002 period_max_us=10120
```

## Field Descriptions

### `hz` — Actual Working Frequency

The real frequency computed over the report window (5 seconds):
```
hz = report_cycles × 1,000,000 / elapsed_report_window_us
```

- Displayed with up to 2 decimal places (e.g., `99.80`)
- At normal operation: **99.0 – 100.0 Hz**
- Computed from wall-clock time, NOT from processing duration

### `report_cycles` — Cycles in Current Report Window

Number of fast-loop cycles executed since the last diagnostic print (5s ago).
- At 100 Hz: approximately 500 per 5-second window
- Reset to 0 after each diagnostic print

### `total_cycles` — Total Cycles Since Boot

Monotonically increasing counter. Never resets within a session.
- Overflows after ~4.29 billion cycles (~497 days at 100 Hz)

### `missed` — Deadline Misses

Number of cycles where `vTaskDelayUntil()` returned late.
- The expected wake time is recorded BEFORE the delay
- If the actual wake time exceeds the expected time, a miss is counted
- Should be **0** under normal operation
- Non-zero values indicate scheduling pressure (e.g., high-priority
  interrupts, WiFi/ETH stack contention on Core 1)

### `processing_us` — Current Cycle Processing Duration

Duration of the most recent fast-input + fast-process + fast-output
execution, in microseconds.
- Measured as `cycle_end_us - cycle_start_us`
- At normal operation: typically **20 – 200 µs**
- If this exceeds **10000 µs**, the cycle has overrun its deadline

### `remaining_us` — Remaining Budget Until Deadline

```
remaining_us = max(0, 10000 - processing_us)
```

- At normal operation: approximately **9800 – 9980 µs** (near 10000)
- Indicates how much headroom remains before deadline violation
- If 0, the processing exceeded the 10 ms budget

### `period_avg_us` — Average Start-to-Start Period

Average time between consecutive cycle starts within the report window.
- At normal operation: **~10000 µs** (10 ms)
- Includes scheduler jitter and vTaskDelayUntil precision

### `period_max_us` — Maximum Start-to-Start Period

Longest start-to-start interval within the report window.
- Reveals jitter spikes
- Values significantly above 10000 µs indicate scheduling anomalies

## Ticking Mechanism

The fast loop uses `vTaskDelayUntil()` for deterministic timing:

```c
TickType_t last_wake_time = xTaskGetTickCount();
const TickType_t period_ticks = pdMS_TO_TICKS(10);

while (1) {
    // ... fast_input, fast_process, fast_output ...
    vTaskDelayUntil(&last_wake_time, period_ticks);
}
```

This ensures each cycle starts at a fixed interval from the **previous cycle
start**, eliminating cumulative jitter that `vTaskDelay()` would cause.

## Interpreting Issues

| Symptom | Likely Cause |
|---------|-------------|
| `hz < 95` | Core 1 overloaded, too many fast_path components |
| `processing_us > 5000` | Expensive computation in fast path |
| `remaining_us = 0` | Cycle overrun — processing exceeds 10 ms |
| `missed > 0` | Scheduler contention, ETH/WIFI ISR latency |
| `period_max_us > 20000` | Occasional long interrupt or task preemption |
| `hz > 100` | Clock drift (ESP32 oscillator tolerance) |
