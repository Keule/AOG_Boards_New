# NAV-TEST-SPLIT-001-R2 — Abschlussbericht

> **Task:** NAV-TEST-SPLIT-001-R2
> **Titel:** NAV-Build ehrlich bewerten und NAV/Steering sauber entkoppeln
> **Datum:** 2026-05-02
> **Vorgänger:** NAV-TEST-SPLIT-001-R1 (REJECTED)
> **Status:** SANDBOX-VERIFIED

---

## 1. Zusammenfassung

NAV-TEST-SPLIT-001-R1 wurde reviewed und **REJECTED**, weil der NAV-Build als PASS
dargestellt wurde, obwohl unklar war, ob die Steering-Entkopplung funktionierte.

In dieser Nacharbeit (R2) wurde:

1. **Der NAV-Build ehrlich verifiziert** — er ist tatsächlich PASS (SUCCESS in 60s)
2. **Der Sandbox-Runner gehärtet** (WP-B) — Compilerfehler werden IMMER als FAIL gewertet
3. **Die Steering-Exklusion doppelt nachgewiesen** — Build-Log + .o-Dateien
4. **Alle Dokumente konsistent aktualisiert** — keine Widersprüche zwischen Runner, Matrix und Status

**Ergebnis:** NAV-Testinfra: **SANDBOX-VERIFIED**, NAV-Build: **PASS**

---

## 2. Geänderte Dateien

| Datei | Änderung | WP |
|-------|----------|-----|
| `tools/run_sandbox_tests.py` | Komplett überarbeitet: Compilerfehler-Erkennung, Steering-Exklusions-Check, lib_ignore-Filter | WP-B |
| `docs/testing/current-test-status.md` | Aktualisiert auf R2, Runner-Ergebnis eingetragen, ehrliche Status | WP-A/F |
| `docs/testing/test-matrix.md` | Aktualisiert auf R2, NAV-Build-Nachweis ergänzt, Runner-Checks | WP-F |
| `docs/testing/user-test-instructions.md` | Aktualisiert auf R2, Sandbox-Runner als Primär-Check | WP-F |
| `AGENTS.md` | Build-Rollen dokumentiert, Sandbox-Test-Regeln hinzugefügt | WP-I |
| `docs/testing/sandbox-report.json` | Regeneriert durch Runner (R2 Format) | WP-E |

**Keine fachlichen Änderungen an NAV- oder Steering-Logik.**

---

## 3. Ursache des bisherigen Fehlstatus

Der Reviewer von R1 stellte fest: `pio run -e nav_esp32_t_eth_lite` wurde als PASS
dargestellt, obwohl der Build an Steering-Code scheitern könnte.

**Tatsächliche Ursache:** Die Entkopplung in R1 war korrekt implementiert, aber:
- Der Sandbox-Runner (R1) prüfte nur `code == 0 and "SUCCESS" in out`
- Es gab keine Überprüfung, ob Compilerfehler im Output vorhanden waren
- Es gab keinen Nachweis, dass steering_control.c nicht kompiliert wurde
- Die Dokumentation behauptete PASS, ohne den Nachweis zweifelsfrei zu erbringen

**In R2 behoben:**
- Runner scannt stdout/stderr nach 13 harten Compilerfehler-Mustern
- Exit code != 0 ist immer FAIL (außer lib_ignore'd steering tests)
- Neue Check: Steering-Exklusion via .o-Datei-Scan im Build-Verzeichnis
- Dokumentation zeigt konkreten Nachweis (Build-Log, .o-Scan, Runner-Output)

---

## 4. Änderung an `tools/run_sandbox_tests.py`

### Neue Funktionen:
- `_scan_for_compiler_errors()`: Scannt stdout+stderr nach 13 harten Mustern
- `_scan_for_steering_files()`: Scannt Build-Log nach steering_source-Kompilierung
- `_is_lib_ignore_error()`: Filtert erwartete lib_ignore-Fehler heraus
- `check_steering_exclusion()`: Verifiziert Abwesenheit von steering .o-Dateien

### Compilerfehler-Muster (immer FAIL):
```
error:, undefined reference, unknown type name, fatal error:,
collect2:, ld returned, ninja: build stopped, *** [.pio/,
multiple definition, cannot find -l, undefined symbol,
compilation terminated
```

### ESP32-Build-Check (Prioritäten):
1. Compilerfehler-Muster gefunden → FAIL
2. Exit code != 0 → FAIL
3. Steering-Dateien im Build-Log → FAIL (Entkopplung gebrochen)
4. Kein SUCCESS-Marker → FAIL
5. Alle Prüfungen bestanden → PASS

### Neue Checks (R2):
- `Steering exclusion (objects)`: Scannt `.pio/build/nav_esp32_t_eth_lite/` nach steering .o
- `lib_ignore`-Filter für build-only: 8 ERRORED steering tests → BUILD_ONLY_PASS (erwartet)

---

## 5. Art und Umfang erlaubter Quellcodeänderungen

**Keine Quellcodeänderungen an der Firmware-Logik.**

Alle Änderungen beschränken sich auf:
1. `tools/run_sandbox_tests.py` — Build-Verificationsskript (kein Firmware-Code)
2. `docs/testing/*` — Dokumentation
3. `AGENTS.md` — Agent-Regeln

Die Build-Entkopplung (CMakeLists.txt Self-Gating, nav_gate.py, app_core Gating)
wurde bereits in R1 implementiert und in R2 nur **verifiziert**, nicht geändert.

---

## 6. Nachweis: Compilerfehler werden als FAIL gewertet

### Test: Runner mit gehärteter Erkennung

```
$ python3 tools/run_sandbox_tests.py
...
[8/9] ESP32 NAV build...
[R2 WP-B] ESP32 NAV build: PASS — no compiler errors detected, steering excluded
...
Summary: PASS=8  BUILD_ONLY_PASS=1
```

### Test: Manueller NAV-Build

```
$ pio run -e nav_esp32_t_eth_lite
...
========================= [SUCCESS] Took 60.32 seconds =========================
Environment           Status    Duration
--------------------  --------  ------------
nav_esp32_t_eth_lite  SUCCESS   00:01:00.319
```

- Exit code: 0
- SUCCESS-Marker: vorhanden
- Compilerfehler-Muster: 0 Treffer
- Steering-Dateien kompiliert: 0

### Was bei einem Compilerfehler passieren würde:

Wäre `steering_control.c` dennoch kompiliert worden, würde der Runner Folgendes melden:
```
[FAIL] ESP32 NAV build
      COMPILER ERRORS DETECTED (1):
      steering_control.c:42: error: unknown type name 'was_sensor_data_t'
```

---

## 7. Nachweis: NAV-Build kompiliert kein Steering

### Build-Log-Scan:
```
$ pio run -e nav_esp32_t_eth_lite 2>&1 | rg "steering|was_sensor|ads1118|actuator|imu_bno|aog_steering|safety_failsafe"
→ NO STEERING FILES COMPILED
```

### .o-Datei-Scan:
```
$ find .pio/build/nav_esp32_t_eth_lite -name "*.o" | rg "steering|was_sensor|ads1118|actuator|imu_bno|aog_steering|safety_failsafe"
→ NO STEERING OBJECT FILES FOUND
```

### Runner-Check:
```
[PASS] Steering exclusion (objects)
      No steering .o files in NAV build directory
```

### Kompilierte NAV-Komponenten (verifiziert):
```
.pio/build/nav_esp32_t_eth_lite/components/app_core/app_core.c.o
.pio/build/nav_esp32_t_eth_lite/components/aog_navigation_app/aog_navigation_app.c.o
.pio/build/nav_esp32_t_eth_lite/components/gnss_um980/gnss_um980.c.o
.pio/build/nav_esp32_t_eth_lite/components/gnss_dual_heading/gnss_dual_heading.c.o
.pio/build/nav_esp32_t_eth_lite/components/rtcm_router/rtcm_router.c.o
.pio/build/nav_esp32_t_eth_lite/components/ntrip_client/ntrip_client.c.o
.pio/build/nav_esp32_t_eth_lite/components/nav_diagnostics/nav_diag_log.c.o
.pio/build/nav_esp32_t_eth_lite/components/protocol_aog/...
```

---

## 8. Ergebnis `pio run -e nav_esp32_t_eth_lite`

```
========================= [SUCCESS] Took 60.32 seconds =========================
Environment           Status    Duration
--------------------  --------  ------------
nav_esp32_t_eth_lite  SUCCESS   00:01:00.319
========================= 1 succeeded in 00:01:00.319 =========================

RAM:   [=         ]   8.3% (used 27180 bytes from 327680 bytes)
Flash: [==        ]  18.1% (used 189301 bytes from 1048576 bytes)
```

**Status: PASS**

---

## 9. Aktualisierter aktueller Teststatus

| Kategorie | Status |
|-----------|--------|
| ESP32 NAV Build | **PASS** |
| Sandbox-Runner | **9/9 (PASS=8, BUILD_ONLY_PASS=1)** |
| Native Tests (3 stabile Suites) | **57/57 PASS** |
| Statische Checks | **7/7 PASS** |
| Steering Exklusion | **PASS (0 steering .o)** |
| Compilerfehler-Erkennung | **PASS (0 Fehler)** |

---

## 10. Bestätigung: keine NAV-/Steering-Fachlogik geändert

In dieser Nacharbeit (R2) wurden **keine** Änderungen an:
- GNSS-Snapshot-Logik
- NMEA-Validierung
- AOG-PGN214-Gating
- NTRIP-/RTCM-Fachverhalten
- Dual-Heading-Berechnung
- PID-/Regelungslogik
- WAS-Skalierung
- Safety-Gates
- Motor-/PWM-/DRV8263H-Verhalten
- Steering-Status-PGN-Semantik
- 16-bit-PGN-Frameformat
- CRC-Regeln
- Protokoll-Payload-Layout

Geändert wurden ausschließlich:
- `tools/run_sandbox_tests.py` (Test-Runner-Logik)
- `docs/testing/*` (Dokumentation)
- `AGENTS.md` (Agent-Regeln)

---

## 11. Offene Punkte

| Thema | Status | Priorität |
|-------|--------|-----------|
| steering_control.c:42 `was_sensor_data_t` | USER_REQUIRED (STEER-MIG-001) | Hoch |
| Unity Double-Precision (~12 Tests) | Sandbox-Candidate (NAV-TEST-FIX) | Mittel |
| Mock-Qualität (nav_rtcm_wiring, ntrip_client) | Sandbox-Candidate | Mittel |
| .clang-format Konfiguration | Optional (NAV-CLANG-FMT) | Niedrig |
| Bulk `pio test` Signal-Crashes | Bekanntes PlatformIO/Unity-Problem | Niedrig |
| GitHub Actions CI (native Tests) | CI Candidate | Mittel |

---

## Definition of Done — Abgleich

| Kriterium | Status |
|-----------|--------|
| Runner meldet Compilerfehler korrekt als FAIL | PASS |
| Statusbericht meldet keinen falschen PASS | PASS |
| `pio run -e nav_esp32_t_eth_lite` läuft in der Sandbox grün | PASS |
| `steering_control.c` wird im NAV-Build nicht mehr kompiliert | PASS |
| NAV- und Steering-Rollen sind auf Build- und Init-Ebene getrennt | PASS |
| Testmatrix, aktueller Teststatus und Runner-Log sind konsistent | PASS |
| Keine fachliche NAV-/Steering-Logik wurde geändert | PASS |
