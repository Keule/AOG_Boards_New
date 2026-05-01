# NAV-DIAG-001: Diagnose, Recovery und Langzeittest-Härtung

## 1. Überblick

NAV-DIAG-001 fügt dem NAV-Device eine zentrale Diagnose-Infrastruktur hinzu:
Status/Fehlerzähler, Recovery-Regeln, rate-limitiertes Logging und einen
Health Snapshot für alle Subsysteme.

## 2. Architektur

```
                    ┌──────────────────────┐
                    │   nav_diagnostics    │
                    │  (neue Komponente)    │
                    ├──────────────────────┤
                    │  nav_health.h/c      │ ← Health/Status Snapshot
                    │  nav_diag_log.h/c    │ ← Rate-limitiertes Logging
                    │  nav_recovery.h/c    │ ← Recovery Rule Evaluator
                    └──────────┬───────────┘
                               │ pollt (read-only)
           ┌───────────────────┼───────────────────┐
           │                   │                   │
   ┌───────┴──────┐   ┌───────┴──────┐   ┌───────┴──────┐
   │  transport_   │   │  gnss_um980  │   │ ntrip_client │
   │  uart (×2)   │   │  (×2)        │   │              │
   └──────────────┘   └──────────────┘   └──────────────┘
   ┌───────┐  ┌───────┐  ┌───────┐  ┌───────┐  ┌────────┐
   │ rtcm  │  │heading│  │aog_nav│  │  tcp  │  │  eth   │
   │router │  │calc   │  │_app   │  │       │  │  hal   │
   └───────┘  └───────┘  └───────┘  └───────┘  └────────┘
```

**Wichtige Designentscheidungen:**
- nav_diagnostics ist eine eigene Komponente (kein app_core-Monolith)
- Liest nur Status (keine Fachlogik, keine Transport-Aufrufe, keine UART-I/O)
- Recovery-Evaluator empfiehlt Aktionen, führt sie aber nicht selbst aus
- Logging ist über Callback anpassbar (ESP_LOGI/W/E auf ESP32)

## 3. Health Snapshot (Teil 1)

### 3.1 nav_health_collector_t

Holder mit Pointern auf alle Subsysteme. Wird beim Systemstart
gewired (`nav_health_collector_set_*()`).

### 3.2 nav_health_snapshot_t

Enthält alle Diagnosefelder:

| Feld | Typ | Beschreibung |
|------|-----|-------------|
| `uart_primary_rx_bytes` | uint32 | Bytes empfangen auf primärer UART |
| `uart_primary_tx_bytes` | uint32 | Bytes gesendet auf primärer UART |
| `uart_primary_rx_overflows` | uint32 | RX Buffer Overflow Zähler |
| `uart_primary_tx_errors` | uint32 | HAL Write Error Zähler |
| `uart_secondary_*` | ... | Gleiche Felder für sekundäre UART |
| `gnss_primary_valid` | bool | GNSS Snapshot valid (position + motion) |
| `gnss_primary_fresh` | bool | GNSS Snapshot innerhalb Freshness |
| `gnss_primary_sentences` | uint32 | Anzahl geparster Sätze |
| `gnss_primary_checksum_err` | uint32 | Checksum-Fehler |
| `gnss_primary_timeout_events` | uint32 | Freshness-Timeouts |
| `gnss_primary_bytes` | uint32 | Empfangene Bytes |
| `gnss_secondary_*` | ... | Gleiche Felder für sekundäres GNSS |
| `heading_valid` | bool | Heading Berechnung gültig |
| `heading_calc_count` | uint32 | Anzahl Heading-Berechnungen |
| `ntrip_state` | enum | NTRIP Status (idle/connecting/connected/error/retry_wait) |
| `ntrip_reconnect_count` | uint32 | NTRIP Reconnect Versuche |
| `ntrip_bytes` | uint32 | RTCM Bytes im NTRIP Buffer |
| `ntrip_last_error` | int | Letzter NTRIP Fehlercode |
| `ntrip_http_status` | int | Letzter HTTP Status |
| `rtcm_bytes_in/out/dropped` | uint32 | RTCM Router Statistiken |
| `rtcm_output_overflows` | uint32 | RTCM Output Overflow Events |
| `aog_output_state` | enum | AOG Ausgabezustand (OK/INVALID/STALE/...) |
| `aog_tx_frames` | uint32 | Gesendete PGN 214 Frames |
| `aog_suppressed` | uint32 | Unterdrückte Ausgabezyklen |
| `aog_hello_count` | uint32 | Gesendete Hello Responses |
| `aog_scan_count` | uint32 | Gesendete Scan Replies |
| `tcp_connected` | bool | TCP Verbindung aktiv |
| `tcp_rx_bytes` | uint32 | Bytes in TCP RX Buffer |
| `tcp_tx_bytes` | uint32 | Bytes in TCP TX Buffer |
| `eth_link_up` | bool | Ethernet Link aktiv |
| `uptime_ms` | uint64 | System-Uptime in ms |
| `last_error_module` | uint32 | Modul-ID des letzten Fehlers |
| `last_error_code` | uint32 | Fehlercode des letzten Fehlers |
| `total_errors` | uint32 | Kumulierte Fehleranzahl |

### 3.3 Fehlermodul-IDs

| ID | Name | Beschreibung |
|----|------|-------------|
| 0 | NONE | Kein Fehler |
| 1 | GNSS_PRIMARY | Primäres GNSS |
| 2 | GNSS_SECONDARY | Sekundäres GNSS |
| 3 | NTRIP | NTRIP Client |
| 4 | TCP | TCP Transport |
| 5 | ETH | Ethernet |
| 6 | UART_PRIMARY | Primäre UART |
| 7 | UART_SECONDARY | Sekundäre UART |
| 8 | RTCM | RTCM Router |
| 9 | AOG | AOG Navigation App |
| 10 | HEADING | Heading Berechnung |

## 4. Rate-limitiertes Logging (Teil 2)

### 4.1 Konzept

Jede Log-Stelle deklariert ein `nav_diag_log_entry_t` mit Mindest-Intervall.
Innerhalb des Intervalls werden Nachrichten unterdrückt, aber mitgezählt.

### 4.2 Standard-Intervalle

| Level | Intervall | Zweck |
|-------|-----------|-------|
| DEBUG | 5000 ms | Debug-Info, max 1× pro 5s |
| INFO  | 2000 ms | Allgemeine Info |
| WARN  | 1000 ms | Warnungen, max 1× pro Sekunde |
| ERROR | 500 ms  | Fehler, bis zu 2× pro Sekunde |

### 4.3 Usage

```c
/* Deklaration (file scope) */
static nav_diag_log_entry_t s_log_warn = NAV_DIAG_LOG_ENTRY_INIT(WARN);

/* Verwendung */
NAV_DIAG_LOG(&s_log_warn, NAV_DIAG_LEVEL_WARN, "NTRIP",
             "connection timeout after %d ms", timeout);
```

### 4.4 ESP32 Integration

Auf ESP32 kann ein Callback gesetzt werden:
```c
nav_diag_log_set_emit_callback(my_esp_log_bridge);
```
Der Callback empfängt Level, Modulname und formatierte Nachricht.

## 5. Recovery-Regeln (Teil 3)

### 5.1 Regeln

| Regel | Trigger | Aktion |
|-------|---------|--------|
| NTRIP Reconnect | State = ERROR oder RETRY_WAIT | `ntrip_client` führt auto-reconnect durch |
| TCP Reconnect | `tcp_connected = false` | NTRIP-TCP Verbindung prüfen |
| Ethernet Reinit | `eth_link_up = false` | Ethernet HAL reinitialisieren |
| GNSS Freshness | `gnss_*_fresh = false` + bytes > 0 | Timeout-Ereignis wurde gezählt |
| UART Errors | `rx_overflow > 0` oder `tx_errors > 0 | Diagnose anzeigen |

### 5.2 Wichtige Eigenschaften

- **Keine blockierenden Sleeps** in Service-Steps
- Recovery-Evaluator ist read-only (empfiehlt, führt nicht aus)
- NTRIP Backoff ist fixed (configurierbar via `reconnect_backoff_ms`)
- Keine Architekturänderung — bestehende State Machines unverändert

## 6. Typische Fehlerbilder

### 6.1 GNSS-Ausfall (UM980 abgezogen)

Symptom:
- `gnss_primary_valid = false`, `gnss_primary_fresh = false`
- `gnss_primary_timeout_events` steigt
- AOG Output State → `GNSS_STALE` → `GNSS_INVALID`
- PGN 214 mit Sentinel-Werten

Diagnose:
- `heading_valid = false` (keine Heading ohne GNSS)
- Recovery: GNSS_RECOVERY flag gesetzt

### 6.2 NTRIP-Verbindungsabbruch

Symptom:
- `ntrip_state = ERROR` → `RETRY_WAIT` → `CONNECTING` (automatisch)
- `ntrip_reconnect_count` steigt
- `rtcm_bytes_in` stoppt
- Fix Quality degradiert (keine Korrekturdaten)

Diagnose:
- `ntrip_last_error` zeigt Fehlercode
- `tcp_connected` könnte false sein

### 6.3 Ethernet-Ausfall

Symptom:
- `eth_link_up = false`
- `tcp_connected = false` (folgt)
- `ntrip_state = ERROR` (folgt)
- AOG UDP TX nicht möglich

Diagnose:
- Recovery Chain: ETH → TCP → NTRIP

### 6.4 UART-Buffer-Overflow

Symptom:
- `uart_primary_rx_overflows` steigt
- `gnss_primary_checksum_err` steigt (unvollständige Sätze)
- `gnss_primary_sentences` bricht ein

Diagnose:
- Baudrate prüfen, GNSS-Rate reduzieren, Buffer-Größe prüfen

## 7. Testablauf

### 7.1 Host-Tests (35 Tests, Teil 4)

```
pio test -e native_test_nav_diagnostics
```

**Health Snapshot (14 Tests):**
1. Init Collector — alle NULL
2. Collect mit NULL Collector — nur uptime
3. Collect mit NULL Subsystemen — graceful zeros
4-6. UART/GNSS Primary/Secondary Feldextraktion
7. Heading valid + calc_count
8. NTRIP State Mapping
9. RTCM Router Statistiken
10. AOG Nav App Output State + TX Counts
11. TCP connected + buffer bytes
12. ETH link up/down
13. Error Recording + Propagation
14. Full Snapshot mit allen Subsystemen

**Rate-Limited Logging (9 Tests):**
15-16. Entry Init (default + custom interval)
17. First emit passes
18. Rate-limited suppress
19. After interval passes again
20. Suppressed count tracking
21. Global stats
22. Level names
23. Error counter increment

**Recovery Evaluation (8 Tests):**
24. All healthy → no actions
25-29. NTRIP/TCP/ETH/GNSS/UART recovery flags
30. Multiple simultaneous issues
31. Needs action true/false

**State Names (4 Tests):**
32-35. NTRIP/AOG/Error Module/Recovery Flag Names

### 7.2 Hardware Smoke Tests (manual)

| Test | Erwartetes Verhalten |
|------|---------------------|
| UM980 abziehen | `gnss_primary_valid=false`, timeout_events++, AOG→GNSS_INVALID |
| Ethernet abziehen | `eth_link_up=false`, `tcp_connected=false`, NTRIP→ERROR |
| NTRIP falsche Credentials | `ntrip_http_status=401`, `ntrip_last_error=AUTH_FAILED` |
| 30 Min Lauf ohne Crash | `total_errors` stabil, keine Memory-Leaks |

## 8. Langzeittest-Protokoll

### 8.1 Vorbereitung

1. Alle Komponenten verbunden (2× UM980, Ethernet, NTRIP-Caster)
2. Serial Monitor aktiv (115200 baud)
3. Health Snapshot alle 5 Sekunden loggen

### 8.2 Testdauer

- **Minimum**: 30 Minuten kontinuierlicher Betrieb
- **Empfohlen**: 2 Stunden (alle Reconnect-Szenarien abdecken)

### 8.3 Prüfpoints

| Zeit | Prüfung |
|------|---------|
| 0 Min | Alle Felder initialisiert, NTRIP connected |
| 5 Min | `gnss_primary_valid=true`, `aog_output_state=OK` |
| 10 Min | Keine UART Overflows, RTCM bytes_in steigt |
| 15 Min | Kabelziehen-Test: Ethernet ab → Recovery sichtbar |
| 20 Min | Ethernet wieder an → auto-reconnect |
| 25 Min | NTRIP Credentials ändern → Error sichtbar |
| 30 Min | `total_errors` dokumentieren, Vergleich mit Start |

### 8.4 Erfolgsriterien

- `[PASS]` Kein Crash / Reset in 30 Minuten
- `[PASS]` NTRIP Reconnect funktioniert (reconnect_count > 0 nach Kabelzug)
- `[PASS]` GNSS Timeout korrekt erkannt (fresh=false nach Abziehen)
- `[PASS]` UART Overflows = 0 (bei korrektem Setup)
- `[PASS]` AOG Output State korrekt (OK im Normalbetrieb)

## 9. Dateien

### Neue Dateien (8)

| Datei | Beschreibung |
|-------|-------------|
| `components/nav_diagnostics/nav_health.h` | Health Snapshot API |
| `components/nav_diagnostics/nav_health.c` | Health Snapshot Implementierung |
| `components/nav_diagnostics/nav_diag_log.h` | Rate-limitiertes Logging API |
| `components/nav_diagnostics/nav_diag_log.c` | Logging Implementierung |
| `components/nav_diagnostics/nav_recovery.h` | Recovery Rule Evaluator API |
| `components/nav_diagnostics/nav_recovery.c` | Recovery Implementierung |
| `components/nav_diagnostics/nav_diagnostics.h` | Top-Level Header |
| `components/nav_diagnostics/CMakeLists.txt` | Build Konfiguration |
| `test/host/test_nav_diagnostics/test_nav_diagnostics.c` | 35 Host-Tests |
| `test/host/test_nav_diagnostics/platformio.ini` | Native Test Konfiguration |
| `docs/hardware/nav-diagnostics.md` | Diese Dokumentation |

### Modifizierte Dateien (1)

| Datei | Änderung |
|-------|---------|
| `extra_scripts/native_test.py` | +transport_tcp, +nav_diagnostics |

### Keine Änderungen an (harte Regeln eingehalten)

- Keine Architectureänderung
- Keine Fachlogik in Transport verschoben
- Kein app_core-Monolith
- Keine Steering-Änderungen (außer buildbedingter Fix)
- Keine blockierenden Service-Steps
- Keine neuen PGNs

## 10. ADR-Konformität

Alle bestehenden ADR-Regeln weiterhin grün:
- `ADR-NO-PHYSICAL-IO-IN-TASK-FAST`: nav_diagnostics hat keine I/O
- `ADR-TRANSPORT-NO-PROTOCOL-LOGIC`: keine Transport-Änderungen
- `ADR-AOG-APP-NO-PHYSICAL-UDP`: keine AOG App Änderungen
- `ADR-GNSS-NO-DIRECT-UART`: keine GNSS Änderungen
