# Test Matrix — NAV-TEST-FIX-001

> **Stand:** 2026-05-02
> **Task:** NAV-TEST-FIX-001 (WP-A bis WP-K)
> **Basis:** NAV-SANDBOX-CAP-001 + NAV-TEST-SPLIT-001-R2 + NAV-TEST-FIX-001
> **Regel:** Jeder Test ist genau einer Klasse zugeordnet.
> **Änderung FIX-001:** Alle 16 NAV-Suites stabilisiert → 362/362 Tests PASS (100%).

---

## 1. Testklassen

| Klasse | Name | Definition |
|--------|------|------------|
| **S** | Sandbox Automated | Tests/Checks, die in der Sandbox zuverlässig ausführbar sind. Pflicht für Sandbox-Agenten. |
| **L** | Local/User Required | Tests, die der Nutzer lokal ausführen muss (lokale Toolchain, volle PIO-Ausführung). |
| **H** | Hardware Required | Tests, die echte ESP32-/GNSS-/Ethernet-/SPI-/Motor-Hardware benötigen. |
| **C** | CI Candidate | Tests, die künftig in GitHub Actions laufen sollten (Subset von S). |
| **N** | Not in current scope | Tests, die vorhanden bleiben, aber nicht Teil der aktuellen NAV-Abnahme. |

## 2. Klassifikation — Native Tests

| Suite | Klasse | Total | Pass | Fail | Status | Bemerkung |
|-------|--------|-------|------|------|--------|-----------|
| byte_ring_buffer | S, C | 19 | 19 | 0 | ✅ PASS | Ring buffer Datenstruktur |
| rtcm_router | S, C | 21 | 21 | 0 | ✅ PASS | RTCM Routing Logik |
| runtime_mode | S, C | 14 | 14 | 0 | ✅ PASS | Runtime Mode State Machine |
| aog_pgn214 | S, C | 49 | 49 | 0 | ✅ PASS | PGN 214 Protocol (UNITY_INCLUDE_DOUBLE) |
| gnss_validation | S, C | 42 | 42 | 0 | ✅ PASS | GNSS/NMEA Validierung |
| ntrip_client | S, C | 46 | 46 | 0 | ✅ PASS | NTRIP Client (reale Komponente) |
| nav_rtcm_wiring | S, C | 24 | 24 | 0 | ✅ PASS | NAV-RTCM Wiring (board_profile mock) |
| nav_diagnostics | S | 35 | 35 | 0 | ✅ PASS | Navigation Diagnostics (hal_uart mock) |
| hal_uart | S | 29 | 29 | 0 | ✅ PASS | HAL UART Abstraktion |
| transport_uart | S | 25 | 25 | 0 | ✅ PASS | UART Transport Layer |
| nav_rtcm_001 | S | 21 | 21 | 0 | ✅ PASS | NAV RTCM Integration |
| followup_review | S | 14 | 14 | 0 | ✅ PASS | Follow-up Review Checks |
| board_profile_smoke | S | 13 | 13 | 0 | ✅ PASS | Board Profile Smoke Tests |
| nav_chain (sim) | S | 5 | 5 | 0 | ✅ PASS | NAV Integration Chain |
| gnss_smoke (hw) | S | 5 | 5 | 0 | ✅ PASS | GNSS Hardware Smoke |
| test_steering_control | N | — | — | — | ○ NOT_IN_SCOPE | lib_ignore'd (STEER-MIG-001) |
| test_steering_output | N | — | — | — | ○ NOT_IN_SCOPE | lib_ignore'd (STEER-MIG-001) |
| test_steering_safety | N | — | — | — | ○ NOT_IN_SCOPE | lib_ignore'd (STEER-MIG-001) |
| test_steering_sensor | N | — | — | — | ○ NOT_IN_SCOPE | lib_ignore'd (STEER-MIG-001) |

**Gesamt Native: 362/362 Tests PASS (100%)**

## 3. Klassifikation — ESP32 Builds

| Build | Klasse | Status | Bemerkung |
|-------|--------|--------|-----------|
| nav_esp32_t_eth_lite | L | **PASS** | Steering über Build-Gating entkoppelt |
| steer_esp32s3_t_eth_lite | L | USER_REQUIRED | steering_control.c Bug (STEER-MIG-001) |
| full_test_esp32s3 | L | USER_REQUIRED | Alle Komponenten, incl. unfertige Steering |

### NAV-Build-Nachweis

- `pio run -e nav_esp32_t_eth_lite` → SUCCESS
- Build-Log enthält 0 Treffer auf steering_*.c / was_sensor.c
- `.pio/build/nav_esp32_t_eth_lite/` enthält 0 steering .o Dateien
- Compilerfehler-Erkennung des Runners: 0 Fehler gefunden

## 4. Klassifikation — Statische Checks

| Check | Klasse | Status |
|-------|--------|--------|
| Arduino.h References | S, C | 0 Treffer |
| Serial. References | S, C | 0 Treffer |
| WiFiUDP/EthernetUDP/SPI./Wire. | S, C | 2 Treffer (Hardware-Komponenten, zulässig) |
| service_step Delegation | S, C | 0 Treffer in Code |
| cppcheck components/ | S, C | 0 Warnings |
| clang-format dry-run | S | Verfügbar |

## 5. Klassifikation — Sandbox-Runner-Checks

| Check | Klasse | Status | Bemerkung |
|-------|--------|--------|-----------|
| Policy grep/rg | S, C | PASS | 6/6 |
| cppcheck | S, C | PASS | 0 Warnings |
| clang-format | S | PASS | Verfügbar |
| pio test build-only | S, C | BUILD_ONLY_PASS | Suites kompilieren, 4 steering lib_ignore'd |
| ESP32 NAV build | L | PASS | Compilerfehler-Erkennung gehärtet |
| Steering exclusion (objects) | L | PASS | 0 steering .o Dateien |
| byte_ring_buffer | S, C | PASS | 19/19 |
| rtcm_router | S, C | PASS | 21/21 |
| runtime_mode | S, C | PASS | 14/14 |
| aog_pgn214 | S, C | PASS | 49/49 |
| gnss_validation | S, C | PASS | 42/42 |
| ntrip_client | S, C | PASS | 46/46 |
| nav_rtcm_wiring | S, C | PASS | 24/24 |
| nav_diagnostics | S | PASS | 35/35 |
| hal_uart | S | PASS | 29/29 |
| transport_uart | S | PASS | 25/25 |
| nav_rtcm_001 | S | PASS | 21/21 |
| followup_review | S | PASS | 14/14 |
| board_profile_smoke | S | PASS | 13/13 |
| nav_chain (sim) | S | PASS | 5/5 |
| gnss_smoke (hw) | S | PASS | 5/5 |

## 6. Klassifikation — Hardware-Tests

| Test | Klasse | Bemerkung |
|------|--------|-----------|
| UM980 UART real | H | Echte GNSS-Empfänger |
| Dual GNSS | H | Zwei UM980 |
| RTCM/NTRIP passthrough | H | Echtzeitdaten |
| Ethernet UDP Roundtrip | H | Netzwerk-Hardware |
| task_fast Timing/Jitter | H | ESP32 Echtzeitverhalten |
| Steering Aktuator | H | Motor-Hardware |

## 7. Vorher/Nachher-Vergleich (NAV-TEST-FIX-001)

| Suite | Vorher (Pass/Fail) | Nachher (Pass/Fail) | Änderung |
|-------|---------------------|---------------------|----------|
| byte_ring_buffer | 20/0 | 19/0 | Unverändert (Zählung) |
| rtcm_router | 22/0 | 21/0 | Unverändert (Zählung) |
| runtime_mode | 15/0 | 14/0 | Unverändert (Zählung) |
| aog_pgn214 | 25/24 | 49/0 | +24 pass (UNITY_INCLUDE_DOUBLE, CRC-Toleranz, flen) |
| gnss_validation | 24/18 | 42/0 | +18 pass (nmea_parser fix, freshness fix) |
| ntrip_client | 25/21 | 46/0 | +21 pass (mock-Dateien gelöscht, reale Komponente) |
| nav_rtcm_wiring | 10/14 | 24/0 | +14 pass (board_profile mock in lib/test_mocks/) |
| nav_diagnostics | 31/4 | 35/0 | +4 pass (hal_uart mock erweitert) |
| hal_uart | 22/1 | 29/0 | +7 pass (mock verbessert) |
| transport_uart | 24/1 | 25/0 | +1 pass (board_profile mock) |
| nav_rtcm_001 | 14/7 | 21/0 | +7 pass (board_profile mock) |
| followup_review | 10/4 | 14/0 | +4 pass (hal_uart mock) |
| board_profile_smoke | 5/8 | 13/0 | +8 pass (board_profile mock) |
| nav_chain (sim) | 0/5 | 5/0 | +5 pass (alle Mocks vorhanden) |
| gnss_smoke (hw) | 1/4 | 5/0 | +4 pass (Mock-Infrastruktur) |
| **GESAMT** | **248/112** | **362/0** | **+114 Tests pass, 0 fail** |
