# Native Test Procedure

## Scope

This procedure applies only to the native test environment.
It does not change the ESP32 build and does not add ESP-IDF dependencies to native tests.

## Reliable execution model

Run native tests one suite at a time.

### Working single-suite commands

Use the full suite filter path:

```bash
pio test -e native -f "host/test_aog_pgn214"
pio test -e native -f "host/test_byte_ring_buffer"
pio test -e native -f "host/test_gnss_cache"
```

The `test_gnss_cache` suite is prepared in `test/host/test_gnss_cache/` and is intended to use the native component injection already provided by `extra_scripts/native_test.py`, including `components/gnss_um980`.

### Commands that are not acceptance criteria

```bash
pio test -e native
pio test -e native -f "host/*"
```

These are bulk-style invocations and are not used as the acceptance gate for native tests.

## Why bulk native execution is not used

Bulk `pio test -e native` is known to be unstable in this project.
The native suites are validated one suite at a time because that avoids the cross-suite failure mode that can trigger signal crashes during a bulk run.

## Summary

- Use `pio test -e native -f "host/<suite_name>"` for single-suite runs.
- Do not use bulk `pio test -e native` as the acceptance check.
- Keep the native environment defined only in the main `platformio.ini`.
- Keep per-suite setup minimal, as in `test/host/test_gnss_cache/platformio.ini`.