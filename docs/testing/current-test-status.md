# Current Test Status

> **Stand:** 2026-05-02
> **Task:** NAV-TEST-FIX-001 (WP-A bis WP-K)
> **Letzte Aktualisierung:** Nach WP-K (Abschlussbericht)

---

## Sandbox-Runner Ergebnis (WP-I)

**Datum:** 2026-05-02
**Runner:** `tools/run_sandbox_tests.py` v3 (NAV-TEST-FIX-001 WP-I)
**Gesamt:** 22 Checks — PASS=20, BUILD_ONLY_PASS=1, NOT_IN_SCOPE=4, FAIL=0, BLOCKED=0

### Phase 1: Static Analysis

| # | Check | Status | Details |
|---|-------|--------|---------|
| 1 | Policy grep/rg checks | PASS | 6/6 Policy-Checks OK |
| 2 | cppcheck | PASS | 0 warnings, 0 errors |
| 3 | clang-format | PASS | Verfügbar |

### Phase 2: Build Checks

| # | Check | Status | Details |
|---|-------|--------|---------|
| 4 | pio test build-only | BUILD_ONLY_PASS | Suites kompilieren, 4 lib_ignore'd (steering) |
| 5 | ESP32 NAV build | PASS | Keine Compilerfehler, keine Steering-Dateien kompiliert |
| 6 | Steering exclusion (objects) | PASS | Keine steering .o im NAV-Build-Verzeichnis |

### Phase 3: Test Suites (16 Suites)

| # | Suite | Klasse | Status | Tests | Details |
|---|-------|--------|--------|-------|---------|
| 7 | byte_ring_buffer | S, C | PASS | 19/19 | Ring buffer |
| 8 | rtcm_router | S, C | PASS | 21/21 | RTCM routing |
| 9 | runtime_mode | S, C | PASS | 14/14 | Runtime mode |
| 10 | aog_pgn214 | S, C | PASS | 49/49 | PGN 214 |
| 11 | gnss_validation | S, C | PASS | 42/42 | GNSS/NMEA |
| 12 | ntrip_client | S, C | PASS | 46/46 | NTRIP client |
| 13 | nav_rtcm_wiring | S, C | PASS | 24/24 | NAV-RTCM wiring |
| 14 | nav_diagnostics | S | PASS | 35/35 | Diagnostics |
| 15 | hal_uart | S | PASS | 29/29 | HAL UART |
| 16 | transport_uart | S | PASS | 25/25 | UART transport |
| 17 | nav_rtcm_001 | S | PASS | 21/21 | NAV RTCM |
| 18 | followup_review | S | PASS | 14/14 | Review checks |
| 19 | board_profile_smoke | S | PASS | 13/13 | Board profile |
| 20 | nav_chain (sim) | S | PASS | 5/5 | Integration |
| 21 | gnss_smoke (hw) | S | PASS | 5/5 | GNSS smoke |

### Phase 4: Not in Scope

| # | Suite | Klasse | Status | Details |
|---|-------|--------|--------|---------|
| 22 | steering_control | N | NOT_IN_SCOPE | lib_ignore'd, STEER-MIG-001 |
| 22 | steering_output | N | NOT_IN_SCOPE | lib_ignore'd, STEER-MIG-001 |
| 22 | steering_safety | N | NOT_IN_SCOPE | lib_ignore'd, STEER-MIG-001 |
| 22 | steering_sensor | N | NOT_IN_SCOPE | lib_ignore'd, STEER-MIG-001 |

**Native Tests Gesamt: 362/362 PASS (100%)**

---

## ESP32 Build

| Build | Status | Fehler |
|-------|--------|--------|
| nav_esp32_t_eth_lite | **PASS** | Keine — Steering entkoppelt |
| steer_esp32s3_t_eth_lite | USER_REQUIRED | steering_control.c (STEER-MIG-001) |
| full_test_esp32s3 | USER_REQUIRED | Alle Komponenten, incl. unfertige Steering |

### NAV-Build-Entkopplung

**Mechanismus:**
1. `extra_scripts/nav_gate.py` schreibt `.aog_build_role = "NAVIGATION"` vor dem Build
2. `CMakeLists.txt` liest den Marker und setzt `AOG_BUILD_ROLE`
3. `app_core/CMakeLists.txt`: `REQUIRES` ist bedingt — Steering nur bei STEERING/FULL_TEST
4. 10 Steering-CMakeLists.txt: `idf_component_register()` wird übersprungen bei NAVIGATION
5. `app_core.c`: Alle Steering-Includes und -Init-Blöcke sind `#if defined(DEVICE_ROLE_STEERING) || defined(DEVICE_ROLE_FULL_TEST)` gegated

---

## Statische Checks

| Check | Status | Ergebnis |
|-------|--------|----------|
| Arduino.h | PASS | 0 Treffer |
| Serial. | PASS | 0 Treffer |
| WiFiUDP/EthernetUDP/SPI./Wire. | PASS | 2 Treffer (Hardware, OK) |
| service_step | PASS | 0 Code-Treffer |
| cppcheck | PASS | 0 Warnings auf components/ |
| clang-format | PASS | Verfügbar |

---

## Mocks und Testhilfen (WP-J)

### lib/test_mocks/board_profile.h + board_profile.c
- **Zweck:** Board-Profile Mock für native Tests (ersetzt ESP32-spezifische Board-Konfiguration)
- **Abgedecktes Verhalten:** Pin-Zuordnung, UART-Konfiguration, Board-Typ-Erkennung
- **Grenzen:** Kein echter Hardware-Zugriff, statische Testdaten

### lib/test_mocks/hal_uart.h + hal_uart.c
- **Zweck:** UART HAL Mock für native Tests (ersetzt ESP32 UART Treiber)
- **Abgedecktes Verhalten:** Init/Deinit, Send/Empfang, Konfiguration, Error-Tracking
- **Grenzen:** Keine echte UART-Kommunikation, Puffer im RAM

### lib/test_mocks/runtime_mode.h + runtime_mode.c
- **Zweck:** Runtime Mode Mock für native Tests
- **Abgedecktes Verhalten:** Mode-Setzen, Abfragen, State Machine
- **Grenzen:** Kein echter Task-Scheduling

### lib/test_mocks/hal_backend.h
- **Zweck:** HAL Backend Stub (leere Implementierung)
- **Abgedecktes Verhalten:** Kompilierbar, keine Funktionalität
- **Grenzen:** Nur Stub, kein echtes Backend

### lib/test_mocks/esp_log.h
- **Zweck:** ESP-IDF Logging Mock
- **Abgedecktes Verhalten:** ESP_LOGI, ESP_LOGW, ESP_LOGE Makros
- **Grenzen:** Output geht an stdout, keine ESP-IDF Log-Infrastruktur

### test/host/mocks/board_profile_mock.c
- **Zweck:** Alternativer Board-Profile Mock (Legacy, wird durch lib/test_mocks/board_profile ergänzt)
- **Abgedecktes Verhalten:** Einfache Pin-Zuordnung
- **Grenzen:** Weniger vollständig als lib/test_mocks/board_profile

### test/host/unity.h
- **Zweck:** Lokale Unity-Header-Kopie (override für PlatformIOs eingebauten Unity)
- **Abgedecktes Verhalten:** Alle Unity-Assertion-Makros, TEST_ASSERT_EQUAL_DOUBLE
- **Grenzen:** Kein override für PlatformIO-Test-Runner selbst

---

## Folgetasks

| ID | Titel | Status |
|----|-------|--------|
| STEER-MIG-001 | steering_control.c was_sensor_data_t Fix | USER_REQUIRED |
| NAV-CLANG-FMT | .clang-format Config erstellen | Optional |
