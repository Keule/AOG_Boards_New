# NAV-RTCM-001: RTCM-Routing (NTRIP → Router → GNSS UART)

## Status: Produktiv abgeschlossen + Nacharbeit

## Grundregel (verbindlich)

**Alle aktiven / angeschlossenen / im Boardprofil konfigurierten RTCM-fähigen
GNSS-Empfänger müssen denselben RTCM-Stream erhalten.**

Im aktuellen Dual-UM980-NAV-Aufbau erhalten GNSS_PRIMARY und GNSS_SECONDARY
RTCM. Die Logik ist generisch: Bei später 3 oder mehr aktiven Empfängern
gilt dieselbe Regel für alle.

**Ein aktiver GNSS-Empfänger ohne produktiven TX-Pfad ist ein Fehlerzustand.**
Weniger RTCM-Ausgänge als aktive RTCM-Ziele ist **kein** akzeptierter
degradierter Produktivmodus.

## Architekturentscheidungen

### RTCM-Zielpfad: Generisch — Alle aktiven GNSS-Empfänger

Die RTCM-Verdrahtung ist nicht auf "immer genau 2" hartkodiert. Stattdessen
iteriert `nav_rtcm_wire_outputs()` über die generische GNSS-Port-Tabelle
des Boardprofils (`board_profile_get_gnss_port_count()` /
`board_profile_get_gnss_port()`).

**Regel:** Für jeden aktiven GNSS-Empfänger im Profil:
1. `board_profile_has_uart(port)` → true?
2. `board_profile_has_uart_tx(port)` → true?
3. `rtcm_router_add_output()` → Erfolg?
4. Wenn einer dieser Schritte fehlschlägt → **System ist NICHT produktiv.**

| Board | UART | TX Pin | RX Pin | Ziel |
|-------|------|--------|--------|------|
| LilyGO T-ETH Lite ESP32 | UART_NUM_1 (GNSS_PRIMARY) | GPIO14 | GPIO16 | UM980 #1 RTCM RXIN |
| LilyGO T-ETH Lite ESP32 | UART_NUM_2 (GNSS_SECONDARY) | GPIO15 | GPIO17 | UM980 #2 RTCM RXIN |
| LilyGO T-ETH Lite ESP32S3 | UART_NUM_1 (GNSS_PRIMARY) | GPIO6 | GPIO4 | UM980 #1 RTCM RXIN |
| LilyGO T-ETH Lite ESP32S3 | UART_NUM_2 (GNSS_SECONDARY) | GPIO7 | GPIO5 | UM980 #2 RTCM RXIN |

**Pin-Wahl-Begruendung:**
- ESP32: GPIO14 und GPIO15 sind nicht durch RMII Ethernet (MAC_RMII) belegt.
- ESP32-S3: GPIO6 und GPIO7 sind nicht durch W5500 SPI belegt.
- Alle gewaehlten GPIOs unterstuetzen UART TX via GPIO-Matrix.

**Voraussetzung:** Hardware-Verdrahtung muss entsprechend umgesetzt werden:
- ESP32 GPIO14 ↔ UM980 #1 RXIN
- ESP32 GPIO15 ↔ UM980 #2 RXIN
- (Analog fuer ESP32-S3 GPIO6/GPIO7)

### RTCM-Datenformat: Variante A — Transparenter Byte-Stream

RTCM-Daten werden als roher Byte-Stream durch den Router geleitet.
Keine RTCM3-Frame-Validierung, keine CRC24Q-Pruefung.

**Begruendung:**
- Der NTRIP-Server liefert bereits valide RTCM3-Frames.
- Eine Frame-Validierung im Router wuerde Latenz erzeugen und
  Komplexitaet hinzufuegen, ohne produktiven Nutzen.
- Der UM980 validiert RTCM-Frames selbststaendig.
- `rtcm_passthrough` ist explizit als transparenter Durchsatz
  benannt — keine Fachlogik.

**Folge:** `rtcm_router` darf beliebige Bytefolgen routen (nicht nur
gueltige RTCM3-Frames). Tests mit beliebigen Byte-Daten sind zulaessig.

### Drop-Policy: Variante A — Drop erlaubt bei Output-Overflow

Wenn ein UART-TX-Buffer voll ist, werden ueberschuessige Bytes verworfen.
Die Source (NTRIP rtcm_buffer) wird trotzdem komplett geleert.
Ein voller Output darf andere Outputs **nicht** blockieren.

**Begruendung:**
1. **Zeitkritische Daten:** RTCM-Korrekturdaten sind nur fuer wenige
   Sekunden gueltig. Eine veraltete Korrektur ist schlimmer als keine.
2. **Backpressure-Kette:** Wuerde der Router Daten in der Source
   zurueckhalten, wuerde der NTRIP-TCP-Buffer volllaufen.
3. **Unabhaengige Outputs:** Ein voller Primary-Buffer blockiert nicht
   den Secondary-Output. Beide Buffer sind isoliert.
4. **Diagnostics:** `bytes_dropped` und `output_overflow_count`
   erlauben die Erkennung von Buffer-Problemen im Betrieb.

**Policy-Zusammenfassung:**
```
Source → gelesen (immer komplett)
  → Output N: geschrieben, Rest gedroppt (wenn voll)
  → Alle Outputs: unabhaengig voneinander
  → Stats: bytes_dropped pro Output, output_overflow_count global
```

## Datenfluss

```text
ntrip_client.rtcm_buffer
  ↓ (rtcm_router_service_step liest von Source — immer komplett)
rtcm_router
  ↓ (schreibt in alle registrierten Outputs — Drop bei Overflow)
  ├→ gnss_uart[0].tx_buffer → UART TX → GNSS #1 RTCM RXIN
  ├→ gnss_uart[1].tx_buffer → UART TX → GNSS #2 RTCM RXIN
  └→ gnss_uart[N].tx_buffer → UART TX → GNSS #N RTCM RXIN (zukunft)
```

### Verdrahtung (app_core.c → nav_rtcm_wiring)

```c
/* Generisch: ueber GNSS-Port-Tabelle iterieren, nicht hartkodiert */
nav_rtcm_target_t targets[NAV_RTCM_MAX_TARGETS];
int target_count = 0;
for (int i = 0; i < board_profile_get_gnss_port_count(); i++) {
    board_uart_port_t port = board_profile_get_gnss_port(i);
    targets[target_count].port = port;
    targets[target_count].tx_buffer = &uart_by_port[port]->tx_buffer;
    target_count++;
}

nav_rtcm_wiring_result_t wiring =
    nav_rtcm_wire_outputs(targets, target_count, &s_rtcm_router);

if (!wiring.productive) {
    ESP_LOGE(TAG, "%s", wiring.error_detail);
    return;  /* NAV-RTCM-001 nicht produktiv */
}
```

### nav_rtcm_wire_outputs() — Pure Function

Extrahiert aus `app_core` als testbare pure Funktion.

**Prueft fuer jeden aktiven GNSS-Empfaenger:**
1. `board_profile_has_uart(port)` → true (Port aktiv im Profil)
2. `board_profile_has_uart_tx(port)` → true (TX-Pin zugewiesen)
3. `tx_buffer != NULL` (Transport-Buffer verdrahtet)
4. `rtcm_router_add_output() >= 0` (Router hat Kapazitaet)

**Bei jedem Fehler:** `result.productive = false`, `result.error_detail`
enthaelt die Fehlerbeschreibung mit "NAV-RTCM-001 not productive: ..."

**Nach Iteration:**
- `registered_output_count == active_target_count` → produktiv
- Andernfalls: Fehler (Sicherheitsnetz, sollte durch early-return nicht erreicht)

### Service-Kette

1. `ntrip_client_service_step()` — liest RTCM von TCP, schreibt in rtcm_buffer
2. `rtcm_router_service_step()` — liest von rtcm_buffer, verteilt an TX-Buffer
3. `transport_uart_service_step()` — sendet TX-Buffer über physische UART

## Architektur-Regeln

| Regel | Status |
|-------|--------|
| rtcm_router bleibt generisch | ✔ Kein NTRIP/UART/HAL/Board-Abhängigkeit |
| rtcm_router kennt keine GNSS-Namen (PRIMARY/SECONDARY) | ✔ Nur Ring-Buffer-Referenzen |
| rtcm_router schreibt nicht physisch UART | ✔ Nur Ring-Buffer-Schreibzugriffe |
| ntrip_client routet nicht selbst | ✔ Router übernimmt Verteilung |
| transport_uart interpretiert kein RTCM | ✔ Reiner Byte-Transport |
| Router-Outputs nur bei vorhandenem TX-Pin | ✔ nav_rtcm_wire_outputs() Prüfung |
| Kein stummes Routing ohne TX-Pin | ✔ productive=false + Fehlermeldung |
| add_output Rueckgabewert wird geprueft | ✔ nav_rtcm_wire_outputs() prueft < 0 |
| Keine starre Zwei-Empfaenger-Annahme | ✔ Generische Iteration über Port-Tabelle |
| Weniger Outputs als Ziele = Fehler | ✔ productive=false |
| RTCM_ROUTER_MAX_OUTPUTS dokumentiert | ✔ Aktuell 2, NAV_RTCM_MAX_TARGETS=4 |
| Keine AOG-PGN-Änderung | ✔ |
| Keine Steering-Änderung | ✔ |

## RTCM Router Kapazitaet

`RTCM_ROUTER_MAX_OUTPUTS` ist aktuell **2**. `NAV_RTCM_MAX_TARGETS` ist **4**
(vorausschauend). Wenn ein Boardprofil mehr als 2 aktive GNSS-Empfaenger hat,
meldet `nav_rtcm_wire_outputs()` einen klaren Fehler:
"not enough RTCM router outputs for active GNSS receivers".

## Boardprofil-Kopplung

### GNSS-Port-Tabelle (generisch, erweiterbar)

```c
// board_profile.c
static const board_uart_port_t s_gnss_ports[] = {
    BOARD_UART_GNSS_PRIMARY,
    BOARD_UART_GNSS_SECONDARY,
    /* Future: BOARD_UART_GNSS_TERTIARY */
};
int board_profile_get_gnss_port_count(void);  // → 2
board_uart_port_t board_profile_get_gnss_port(int index);
```

Hinzufuegen eines weiteren Empfängers erfordert nur:
1. `BOARD_UART_GNSS_TERTIARY` zum Enum in `board_profile.h`
2. Ein Zeile in `s_gnss_ports[]` in `board_profile.c`
3. Pins in `board_profile_get_uart_pins()`
4. `RTCM_ROUTER_MAX_OUTPUTS` und `NAV_RTCM_MAX_TARGETS` ggf. anpassen

### `board_profile_has_uart_tx(port)`
Gibt `true` zurück, wenn der UART-Port existiert UND ein gültiger
TX-Pin zugewiesen ist (nicht `BOARD_PIN_UNASSIGNED`).

### Kein stummes Routing ohne TX-Pin
Wenn ein GNSS-UART keinen TX-Pin hat, ist das ein **Fehlerzustand**.
Daten in einen TX-Buffer zu schreiben, dessen UART physisch keinen
TX-Pin hat, wuerde bedeuten:
- Daten gehen verloren ohne `bytes_dropped` zu zaehlen.
- Kein Diagnostics-Weg, um den Datenverlust zu erkennen.
- ESP-IDF `UART_PIN_NO_CHANGE` laesst den TX-Pin unkonfiguriert.

## Stats / Diagnose

### RTCM Router Stats (`rtcm_stats_t`)

| Feld | Typ | Beschreibung |
|------|-----|-------------|
| `bytes_in` | `uint32_t` | Vom Source gelesene Bytes |
| `bytes_out` | `uint32_t` | An Outputs geschriebene Bytes (Summe aller) |
| `bytes_dropped` | `uint32_t` | Bytes die nicht in Output passten (Drop-Policy) |
| `last_activity_us` | `uint64_t` | Letzte Aktivität (µs) |

### Per-Output Stats (`rtcm_output_t`)

| Feld | Typ | Beschreibung |
|------|-----|-------------|
| `bytes_forwarded` | `uint32_t` | Weitergeleitete Bytes |
| `bytes_dropped` | `uint32_t` | Gedroppte Bytes (bei vollem Buffer) |
| `enabled` | `bool` | Output aktiv oder deaktiviert |

### Transport UART TX-Stats (`transport_uart_stats_t`)

| Feld | Typ | Beschreibung |
|------|-----|-------------|
| `tx_bytes_out` | `uint32_t` | Gesendete Bytes |
| `tx_errors` | `uint32_t` | HAL Schreibfehler |
| `tx_overflows` | `uint32_t` | TX-Buffer-Overflow-Events |
| `tx_partial_writes` | `uint32_t` | Teilweise HAL-Schreibvorgänge |

## Fehlerfälle

### NAV-RTCM-001 nicht produktiv (hard error)
- Ein aktiver GNSS-Empfaenger hat keinen TX-Pin → `ESP_LOGE` + Init-Abbruch
- `rtcm_router_add_output()` schlägt fehl → `ESP_LOGE` + Init-Abbruch
- Weniger Outputs als aktive RTCM-Ziele → `ESP_LOGE` + Init-Abbruch
- Keine aktiven GNSS-Empfaenger → `ESP_LOGE` + Init-Abbruch
- **Es ist nicht zulaessig, mit weniger Outputs produktiv weiterzulaufen.**

### TX-Buffer voll (Output Overflow)
- `rtcm_router` inkrementiert `bytes_dropped` und `output_overflow_count`.
- Source wird trotzdem komplett geleert (Drop-Policy).
- Andere Outputs sind **nicht** betroffen (unabhaengige Buffer).

### HAL-Schreibfehler
- `tx_errors` wird inkrementiert.
- Bytes bleiben im TX-Buffer (peek/consume — kein Datenverlust).
- Naechster Service-Step versucht erneut.

### NTRIP Disconnect
- `rtcm_buffer` wird geleert.
- RTCM Router liest 0 Bytes → nichts wird verteilt.
- Nach Reconnect fließt RTCM automatisch wieder.

## Testablauf

### Host-/Sim-Tests (gcc native)

| Test | Datei | Ergebnis |
|------|-------|---------|
| **nav_rtcm_wire_outputs() Wiring** | test_nav_rtcm_wiring.c (23 Tests) | ✔ |
| Generisch: 1 Empfänger → 1 Output | test_single_active_receiver_one_output | ✔ |
| Generisch: 2 Empfänger → 2 Outputs | test_two_active_receivers_two_outputs | ✔ |
| Null TX-Buffer → nicht produktiv | test_active_receiver_null_tx_buffer_not_productive | ✔ |
| Router-Kapazität überschritten → Fehler | test_router_capacity_exceeded_not_productive | ✔ |
| output_count == active_target_count | test_output_count_equals_active_target_count | ✔ |
| Weniger Outputs als Ziele → Fehler | test_fewer_outputs_than_targets_not_productive | ✔ |
| Voller Output blockiert andere nicht | test_full_output_does_not_block_other_outputs | ✔ |
| Drop-Stats pro Output korrekt | test_drop_stats_per_output_correct | ✔ |
| **NAV-RTCM-001 Tests** | test_nav_rtcm_001.c (21 Tests) | ✔ |
| Boardprofil TX-Pins produktiv | test_nav_rtcm_001.c | ✔ |
| GNSS-Port-Iteration API | test_gnss_port_count_is_extensible | ✔ |
| **Boardprofil-Compile-Smoke** | test_board_profile_smoke.c (13 Tests) | ✔ |
| GNSS Primary/Secondary Pins gültig | test_board_profile_smoke.c | ✔ |
| BOARD_PIN_UNASSIGNED Sentinel | test_board_profile_smoke.c | ✔ |
| **Router Tests** | test_rtcm_router.c (21 Tests) | ✔ |
| RTCM-Routing zu beiden Outputs | test_both_outputs_identical_bytes | ✔ |
| Drop-Policy + Stats | test_output_buffer_full_drops_data | ✔ |
| **Transport UART** | test_transport_uart.c (25 Tests) | ✔ |
| **Byte Ring Buffer** | test_byte_ring_buffer.c (19 Tests) | ✔ |
| **Runtime Mode** | test_runtime_mode.c (14 Tests) | ✔ |
| **Followup Review Regression** | test_followup_review.c (14 Tests) | ✔ |
| **NTRIP Client** | test_ntrip_client.c (46/47 Tests) | 1 FAIL* |

*Known pre-existing: `test_retry_resets_request_offset` — nicht durch NAV-RTCM-001 verursacht.

### Hardware-Smoke-Test
1. NTRIP Fake-Caster liefert RTCM-Daten
2. Alle UART TX-Zähler steigen (`tx_bytes_out`)
3. `output_overflow_count` bleibt 0 bei ausreichend Buffer
4. Keine Crashes

## Nicht Teil dieses Tasks

- RTCM3-Framevalidierung (0xD3 + Length + CRC24Q)
- RTCM decodieren (MSP/MTK1005)
- UM980 automatisch konfigurieren (NMEA SET)
- AOG PGN 214 (Steering)
- Heading-Berechnung (Dual-GNSS)
- Autosteering
- Web-/OTA-/UI-Arbeiten
- Neue AOG-PGN-Ausgabe
- GNSS-Validierung
- Dual-Heading-Logik-Erweiterung
- NTRIP-Feature-Erweiterung
- Arduino-Abhängigkeiten
- Direkte physische I/O im rtcm_router oder task_fast
- Änderung der NAV→STEER-Taskreihenfolge

## Future Work (nicht Teil von NAV-RTCM-001)

### RTCM3 Frame-aware Validation

**TODO:** RTCM3 frame-aware validation can be implemented as a separate future task.

Die aktuelle Implementierung behandelt RTCM als transparenten Byte-Stream
(NAV-RTCM-001 Variante A). Eine Frame-aware Validierung wuerde umfassen:

- Erkennung von RTCM3-Frame-Beginn (Preamble `0xD3`)
- Frame-Laenge validieren (Length-Field)
- CRC24Q-Pruefung auf Frame-Ebene
- Frame-boundary-aware Routing (kein Mitten-im-Frame-Drop)
- Diagnostic-Stats: frames_valid, frames_invalid, frames_partial

**Begruendung fuer spaetere Implementierung:**
- Der NTRIP-Server liefert bereits valide RTCM3-Frames.
- Der UM980 validiert Frames eigenstaendig.
- Frame-Validierung wuerde CPU-Zeit im Service-Loop verbrauchen.
- Drop-Entscheidungen sollten idealerweise Frame-boundary-respektieren
  (ganzer Frame droppen oder ganzen Frame durchreichen, kein halber Frame).

**Voraussetzungen:**
- MUSS als separate Komponente implementiert werden (z.B. `rtcm_frame_validator`).
- DARF NICHT den generischen `rtcm_router` veraendern.
- DARF NICHT `nav_rtcm_wiring` veraendern.
- Tests muessen Frame-Boundary-Corruption-Szenarien abdecken.
