# Nutzer-Testanleitung

> **Stand:** 2026-05-02
> **Task:** NAV-TEST-FIX-001 (WP-A bis WP-K)
> **Änderung FIX-001:** Alle 16 NAV-Suites 100% grün, Sandbox-Runner v3

---

## 1. Sandbox-Runner (empfohlen)

**Kommando:**
```bash
python3 tools/run_sandbox_tests.py
```

**Erwartung:**
- 22 Checks: PASS=20, BUILD_ONLY_PASS=1, NOT_IN_SCOPE=4
- FAIL=0, BLOCKED=0
- 362/362 native Tests PASS
- ESP32 NAV build: PASS
- Steering exclusion: PASS
- JSON-Report: `docs/testing/sandbox-report.json`

**Ausgabeformat:**
```
  #  Suite                         Cls    Status             Tests     Exit  Details
--------------------------------------------------------------------------------------------------------------
  1  Policy grep/rg checks         C      PASS               —            0  all green
  2  cppcheck                      C      PASS               —            0  all green
  ...
  7  byte_ring_buffer              S, C   PASS               19/19        0  all green
  ...
```

**Für den Reviewer zurückgeben:**
- Vollständiger Runner-Output
- Bestätigung: 0 FAIL, 0 BLOCKED
- Bestätigung: 362/362 Tests PASS

---

## 2. NAV ESP32 Build (manuell)

**Kommando:**
```bash
pio run -e nav_esp32_t_eth_lite
```

**Erwartung:**
```
========================= [SUCCESS] =========================
Environment           Status    Duration
--------------------  --------  ------------
nav_esp32_t_eth_lite  SUCCESS   00:01:00
```

**Nachweis steering_exclusion:**
```bash
find .pio/build/nav_esp32_t_eth_lite -name "*.o" | rg "steering|was_sensor|ads1118|actuator|imu_bno|aog_steering|safety_failsafe"
# Erwartung: keine Treffer
```

---

## 3. Native Tests — Einzel-Suite-Ausführung (zuverlässig)

> **WICHTIG:** `pio test` Bulk-Ausführung ist instabil (Signal-Crashes).
> Einzel-Suite-Ausführung mit vollem Filter-Pfad ist zuverlässig.

**Kommando:**
```bash
pio test -e native -f "host/test_byte_ring_buffer"
pio test -e native -f "host/test_rtcm_router"
pio test -e native -f "host/test_runtime_mode"
pio test -e native -f "host/test_aog_pgn214"
pio test -e native -f "host/test_gnss_validation"
pio test -e native -f "host/test_ntrip_client"
pio test -e native -f "host/test_nav_rtcm_wiring"
pio test -e native -f "host/test_nav_diagnostics"
pio test -e native -f "host/test_hal_uart"
pio test -e native -f "host/test_transport_uart"
pio test -e native -f "host/test_nav_rtcm_001"
pio test -e native -f "host/test_followup_review"
pio test -e native -f "host/test_board_profile_smoke"
pio test -e native -f "sim/test_nav_chain"
pio test -e native -f "hardware/test_gnss_smoke"
```

**Erwartung je Suite:** Alle `[PASSED]`, Summary zeigt 100%.

| Suite | Erwartung |
|-------|-----------|
| byte_ring_buffer | 19 test cases: 19 succeeded |
| rtcm_router | 21 test cases: 21 succeeded |
| runtime_mode | 14 test cases: 14 succeeded |
| aog_pgn214 | 49 test cases: 49 succeeded |
| gnss_validation | 42 test cases: 42 succeeded |
| ntrip_client | 46 test cases: 46 succeeded |
| nav_rtcm_wiring | 24 test cases: 24 succeeded |
| nav_diagnostics | 35 test cases: 35 succeeded |
| hal_uart | 29 test cases: 29 succeeded |
| transport_uart | 25 test cases: 25 succeeded |
| nav_rtcm_001 | 21 test cases: 21 succeeded |
| followup_review | 14 test cases: 14 succeeded |
| board_profile_smoke | 13 test cases: 13 succeeded |
| nav_chain | 5 test cases: 5 succeeded |
| gnss_smoke | 5 test cases: 5 succeeded |

**Gesamt: 362 test cases: 362 succeeded**

---

## 4. Statische Checks

**Policy-Checks:**
```bash
rg "Arduino\.h" components/ src/
rg "Serial\." components/ src/
rg "service_step" components/ src/
# Erwartung: 0 Treffer bei allen
```

**cppcheck:**
```bash
cppcheck --enable=warning,performance,portability --suppress=missingInclude --suppress=unusedFunction -q components/
# Erwartung: 0 warnings, 0 errors
```

---

## 5. Local/User Tests (USER_REQUIRED)

Diese Tests müssen lokal mit voller ESP32-Toolchain ausgeführt werden:

```bash
# Steering Build (benötigt STEER-MIG-001 Fix)
pio run -e steer_esp32s3_t_eth_lite

# Full Test Build
pio run -e full_test_esp32s3
```

---

## 6. Hardware-Tests (HARDWARE_REQUIRED)

Folgende Tests benötigen echte Hardware und können nicht in der Sandbox ausgeführt werden:

| Test | Hardware | Ausführung |
|------|----------|------------|
| UM980 UART real | 1× UM980 GNSS | `pio run -t upload -e nav_esp32_t_eth_lite && pio device monitor` |
| Dual GNSS Heading | 2× UM980 | Manuelles Test-Skript |
| RTCM/NTRIP passthrough | NTRIP-Server + ESP32 | Manuelles Test-Skript |
| Ethernet UDP | W5500 SPI + Switch | Manuelles Test-Skript |
| task_fast Timing | ESP32 + Logic Analyzer | Manuelles Test-Skript |
| Steering Aktuator | ESP32-S3 + Motor | `pio run -t upload -e steer_esp32s3_t_eth_lite` |

---

## 7. Klassifikation Übersicht

| Klasse | Suites | Ausführung |
|--------|--------|------------|
| **S** (Sandbox) | 15 native + 3 static + 3 build = 21 | Automatisch via Runner |
| **L** (Local) | 2 ESP32 Builds | Nutzer lokal |
| **H** (Hardware) | 6 Hardware-Tests | Nutzer mit Hardware |
| **C** (CI) | 7 native Suites | Subset von S, für GitHub Actions |
| **N** (Not in scope) | 4 steering Suites | lib_ignore'd, STEER-MIG-001 |
