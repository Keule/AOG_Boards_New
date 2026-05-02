# AGENTS

Verbindliche Prozessdokumentation befindet sich unter:

- `docs/process/workflow.md`
- `docs/process/task-types.md`
- `docs/process/definition-of-done.md`
- `docs/process/review-rules.md`
- `docs/process/deviations.md`
- `docs/process/lessons-learned.md`

Zusätzlich gelten die rollenbezogenen Regeln unter `agents/`.

## Build-Rollen (NAV-TEST-SPLIT-001-R2)

### NAV-Build (`nav_esp32_t_eth_lite`)

- **DEFINES:** `DEVICE_ROLE_NAVIGATION=1`
- **Kompiliert:** Alle NAV-Komponenten, alle Runtime/HAL/Protocol-Komponenten
- **Kompiliert NICHT:** Steering-Komponenten (steering_control, steering_output, steering_safety, steering_diagnostics, was_sensor, ads1118, actuator_drv8263h, imu_bno085, aog_steering_app, safety_failsafe)
- **Mechanismus:** `extra_scripts/nav_gate.py` → `.aog_build_role` Marker → CMakeLists.txt Self-Gating
- **Test:** `pio run -e nav_esp32_t_eth_lite` muss SUCCESS ergeben

### Steering-Build (`steer_esp32s3_t_eth_lite`)

- **DEFINES:** `DEVICE_ROLE_STEERING=1`
- **Kompiliert:** Alle Komponenten (NAV + Steering)
- **Status:** USER_REQUIRED (steering_control.c Bug, STEER-MIG-001)

### Full-Test-Build (`full_test_esp32s3`)

- **DEFINES:** `DEVICE_ROLE_FULL_TEST=1`
- **Kompiliert:** Alle Komponenten
- **Status:** USER_REQUIRED

## Sandbox-Test-Regeln

1. **Compilerfehler sind IMMER FAIL.** UNSTABLE ist ausschließlich für Signal-Crashes nach Test-Abschluss reserviert.
2. **Hardware-Tests dürfen nicht als PASS markiert werden.** Klasse H ist immer USER_REQUIRED.
3. **Bulk `pio test -e native` ist UNSTABLE.** Einzel-Suite-Ausführung ist die verlässliche Methode.
4. **Steering-Tests (N-Klasse) sind NOT_IN_SCOPE.** lib_ignore'd im native-Environment.
5. **Sandbox-Runner:** `python3 tools/run_sandbox_tests.py` — 9 Checks, 0 FAIL = Sandbox-Verified.
