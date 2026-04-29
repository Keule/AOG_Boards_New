# Testing & CI

## Overview

Das Projekt verwendet PlatformIO mit Unity als Test-Framework. Es gibt drei Test-Ebenen:

1. **Host-Tests (native)** — laufen auf dem Entwicklungsrechner, kein ESP32-Hardware nötig
2. **Simulations-Tests** — zukünftige Integrationstests für ganze Komponentenketten
3. **Build-Checks** — Cross-Compilation aller ESP32-Profile auf CI

## Host-Tests (Native)

### Voraussetzung

- PlatformIO CLI installiert (`pip install platformio`)
- Unity Test-Framework (wird von PlatformIO automatisch bereitgestellt)

### Ausführen

Alle nativen Tests ausführen:

```bash
pio test -e native
```

Einen einzelnen Test ausführen:

```bash
pio test -e native -f test_byte_ring_buffer
pio test -e native -f test_ntrip_client
pio test -e native -f test_rtcm_router
```

### Teststruktur

```
test/
  host/
    test_byte_ring_buffer/
      test_byte_ring_buffer.c    # 19 Tests: init, write, read, wrap-around, overflow
    test_ntrip_client/
      test_ntrip_client.c         # 16 Tests: state machine, transitions, RTCM forwarding
    test_rtcm_router/
      test_rtcm_router.c          # 15 Tests: init, output mgmt, forwarding, stats
  sim/
    test_nav_chain/
      test_nav_chain.c            # Platzhalter fuer kuenftige Simulations-Tests
```

### Was wird getestet?

| Komponente | Tests | Beschreibung |
|------------|-------|-------------|
| `byte_ring_buffer` | 19 | Init, Write/Read, Wrap-around, Overflow-Zähler, NULL-Sicherheit |
| `ntrip_client` | 16 | State-Machine, Transitionen, Skeleton-Autoprogression, RTCM-Forwarding |
| `rtcm_router` | 15 | Init, Output-Registrierung, Daten-Forwarding, Stats, Drop-Handling |

### Wichtig für Host-Tests

- Native Tests dürfen **keine ESP-IDF-Hardwareabhängigkeiten** haben
- Nur reine Logik-Komponenten (ohne HAL/UART/SPI Aufrufe) sind testbar auf Host
- Die Sourcedateien der Komponenten werden über Include-Pfade eingebunden, nicht als ESP-IDF-Komponente

## ADR Compliance Checks

### Voraussetzung

- Python 3.11+
- PyYAML (`pip install pyyaml`)

### Ausführen

```bash
python tools/adr_checks/check_all.py
```

Mit explizitem Repo-Root (falls nicht vom Script-Verzeichnis aus aufgerufen):

```bash
python tools/adr_checks/check_all.py --repo-root /pfad/zum/repo
```

### Regeln

| Regel-ID | Beschreibung |
|----------|-------------|
| `ADR-NO-PHYSICAL-IO-IN-TASK-FAST` | task_fast/runtime_fast darf keine physische UART/UDP/TCP/SPI-I/O ausführen |
| `ADR-TRANSPORT-NO-PROTOCOL-LOGIC` | Transport-Komponenten dürfen keine AOG/NMEA/RTCM-Fachlogik enthalten |
| `ADR-AOG-APP-NO-PHYSICAL-UDP` | AOG-Apps dürfen nicht direkt physisch UDP senden |
| `ADR-GNSS-NO-DIRECT-UART` | gnss_um980 darf nicht direkt UART/HAL lesen |

### Regeln hinzufügen

Neue Regeln in `tools/adr_checks/adr_rules.yaml` ergänzen:

```yaml
  - id: ADR-NEUE-REGEL
    description: "Beschreibung der Regel"
    include:
      - "components/komponente/**"
    forbidden_patterns:
      - "verbotenes_pattern"
```

### Exit-Codes

| Code | Bedeutung |
|------|-----------|
| 0 | Alle Regeln bestanden |
| 1 | Mindestens eine Regelverletzung |
| 2 | Fehler (Rules-Datei oder Repo-Root nicht gefunden) |

## CI Pipeline (GitHub Actions)

Die CI-Pipeline ist in `.github/workflows/ci.yml` definiert und besteht aus drei Jobs:

### Job 1: ADR Compliance
- Installiert Python + PyYAML
- Führt `tools/adr_checks/check_all.py` aus
- Fail bei Regelverletzungen

### Job 2: Native Tests
- Installiert PlatformIO
- Führt `pio test -e native` aus
- Fail bei Testfehlern

### Job 3: Build Matrix
- Baut alle drei ESP32-Profile parallel:
  - `nav_esp32_t_eth_lite`
  - `steer_esp32s3_t_eth_lite`
  - `full_test_esp32s3`
- Zusätzliche Checks: Arduino-Usage, leere SRCS, Komponentenstruktur

### Trigger
- Push auf `main` oder `develop`
- Pull Request gegen `main`

## Regel: Produktive Tasks müssen Tests ergänzen

Jeder produktive Task, der Logik in einer testbaren Komponente ändert oder neu erstellt, **muss** zugehörige Host-Tests ergänzen. Die Test-Datei muss in `test/host/` unter dem Komponentennamen angelegt werden.

Ausnahme: Komponenten mit Hardware-Abhängigkeit (HAL, Transport) können erst getestet werden, wenn Mock-Interfaces bereitstehen.
