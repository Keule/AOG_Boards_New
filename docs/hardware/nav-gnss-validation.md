# NAV-GNSS-VALID-001: GNSS/NMEA-Datenvalidierung und Snapshot-Härtung

## Status: NACHARBEIT ABGESCHLOSSEN

## Übersicht

Härtung der GNSS-Datenkette: **UM980 NMEA → gnss_um980 → GNSS Snapshot → Status/Fix/RTK/Freshness**.

## Architektur

```
UM980 Receiver
     │  UART RX (NMEA bytes)
     ▼
transport_uart (rx_buffer)
     │  byte_ring_buffer_t
     ▼
gnss_um980 (per instance, buffer-based)
     │  nmea_parser → GGA / RMC / GST (strict checksum validation)
     │  rebuild → gnss_snapshot_t (Variant A validity model)
     ▼
gnss_snapshot_t (unified view)
     │  position_valid / motion_valid / accuracy_valid / valid / fresh
     │  status_reason, fix_quality, rtk_status, correction_age, accuracy
     ▼
Downstream Consumers
  ├─ NAV-HEADING-001: position_valid + fresh (heading from 2 antennas)
  └─ NAV-AOG-001:     valid + fresh (full navigation solution)
```

## NMEA-Checksum-Regel (Pflicht 1)

Ein Satz wird **nur** verarbeitet wenn:
- Er mit `$` beginnt
- Er ein `*` enthält
- Nach `*` genau **zwei hexadezimale** Checksum-Zeichen vorhanden sind
- Die XOR-Checksumme über alle Bytes zwischen `$` (exklusiv) und `*` (exklusiv) korrekt ist
- Der Satz syntaktisch vollständig abgeschlossen ist (`\r\n` oder `\n`)

**Ungültige Sätze werden:**
- Verworfen (keine Datenübernahme)
- `checksum_error` Counter erhöht
- Bestehende Snapshot-Felder **nicht überschrieben**
- `nmea_finalize_sentence()` wird **nicht** aufgerufen bei ungültiger Checksumme

**Nicht erlaubt:**
- Sätze ohne Checksumme als gültig akzeptieren ✗
- Checksum-Fehler zählen aber Daten trotzdem übernehmen ✗

## Snapshot-Validitätsmodell (Pflicht 3 — Variante A)

### Getrennte Validitätsflags

| Flag | Bedingung | Beschreibung |
|------|-----------|--------------|
| `position_valid` | Fresh GGA mit `fix_quality` ∈ {1,2,3,4,5} | Position verfügbar |
| `motion_valid` | Fresh RMC mit `status_valid == true` (Status A) | Bewegungsdaten verfügbar |
| `accuracy_valid` | Fresh GST empfangen | Genauigkeitsdaten verfügbar (optional) |
| `valid` | `position_valid AND motion_valid` | Vollständige Navigationslösung |
| `fresh` | `valid AND no staleness` | Alle Daten aktuell |

### Status-Reason Codes

| Code | Bedeutung |
|------|-----------|
| `GNSS_REASON_NONE` | Alles OK |
| `GNSS_REASON_NO_FIX` | GGA fix_quality = 0 |
| `GNSS_REASON_RMC_VOID` | RMC Status = V (void) |
| `GNSS_REASON_NO_GGA` | Noch kein GGA empfangen |
| `GNSS_REASON_NO_RMC` | Noch kein RMC empfangen |
| `GNSS_REASON_STALE_GGA` | GGA Freshness-Timeout abgelaufen |
| `GNSS_REASON_STALE_RMC` | RMC Freshness-Timeout abgelaufen |
| `GNSS_REASON_UNKNOWN_FIX` | GGA fix_quality unbekannt (≥6) |

### Consumer-Nutzung

- **NAV-HEADING-001** braucht: `position_valid + fresh`
- **NAV-AOG-001** braucht: `valid + fresh` (position_valid AND motion_valid)

## Satztyp-spezifische Freshness (Pflicht 4)

Per-Satz-Typ getrennte Zeitstempel und Freshness-Prüfung:

| Zeitstempel | Bedeutung |
|-------------|-----------|
| `last_gga_time_ms` | Letztes gültiges GGA (0 = nie) |
| `last_rmc_time_ms` | Letztes gültiges RMC (0 = nie) |
| `last_gst_time_ms` | Letztes gültiges GST (0 = nie) |

### Freshness-Regeln
- `position_valid = false` wenn GGA timeout abgelaufen
- `motion_valid = false` wenn RMC timeout abgelaufen
- `accuracy_valid = false` wenn GST timeout abgelaufen
- `valid = false` wenn position_valid oder motion_valid false
- `fresh = false` wenn valid false
- Default Timeout: **2000 ms**
- Konfigurierbar: [100 ms, 30000 ms]

## Fix-Qualität und RTK-Status (Pflicht 5)

### fix_quality Mapping

| GGA fix_quality | `gnss_fix_quality_t` | `gnss_rtk_status_t` |
|-----------------|---------------------|---------------------|
| 0 | `GNSS_FIX_NONE` | `GNSS_RTK_NONE` |
| 1 | `GNSS_FIX_SINGLE` | `GNSS_RTK_NONE` |
| 2 | `GNSS_FIX_DGPS` | `GNSS_RTK_NONE` |
| 3 | `GNSS_FIX_PPS` | `GNSS_RTK_NONE` |
| 4 | `GNSS_FIX_RTK_FIXED` | `GNSS_RTK_FIXED` |
| 5 | `GNSS_FIX_RTK_FLOAT` | `GNSS_RTK_FLOAT` |
| 6+ | `GNSS_FIX_UNKNOWN` | `GNSS_RTK_NONE` |

### Robustheitsregeln
- **Unbekannte fix_quality (≥6)** → `GNSS_FIX_UNKNOWN`, `position_valid = false`, `status_reason = UNKNOWN_FIX`
- **fix_quality = 0** → `position_valid = false`, `status_reason = NO_FIX`
- **GGA mit leeren Koordinaten** → `position_valid = false` (fix_quality = 0)

## Correction Age (Pflicht 6)

### Semantik
- `correction_age_valid`: `true` wenn GGA-Feld 11 (age_diff) vorhanden und nicht leer
- `correction_age_s`: Alter der DGPS-Korrektur in Sekunden (nur gültig wenn `correction_age_valid = true`)
- Leeres Feld → `correction_age_valid = false`, `correction_age_s = 0.0`
- `0.0` mit `correction_age_valid = true` → gültiger Wert (Korrektur genau jetzt empfangen)

## GST-Integration (Pflicht 7)

### Vollständig integrierte Felder
- `std_lat`: Latitude-Fehler 1σ (Meter) aus GST Feld 5
- `std_lon`: Longitude-Fehler 1σ (Meter) aus GST Feld 6
- `std_alt`: Altitude-Fehler 1σ (Meter) aus GST Feld 7

Zusätzlich geparst aber nicht im Snapshot gespeichert:
- `total_rms`, `std_major`, `std_minor`, `orientation`

### GST ist optional
- Ohne GST: `accuracy_valid = false`, std_* = 0.0
- Mit GST: `accuracy_valid = true` (innerhalb Freshness-Timeout)
- Snapshot `valid` ist **unabhängig** von GST (nur GGA+RMC nötig)

## Dual-Receiver-Isolation (Pflicht 8)

Jede `gnss_um980_t`-Instanz ist **vollständig isoliert**:
- Eigener `nmea_parser_t` (inkl. State Machine, Buffer)
- Eigene `gga/rmc/gst` Daten + valid-Flags
- Eigener `gnss_snapshot_t`
- Eigene Counter (sentences_parsed, checksum_errors, overflow_errors, timeout_events)
- Eigene Freshness-Konfiguration
- Eigene Dirty-Flags

**Nachgewiesen durch Tests:**
- Checksum-Fehler in Secondary beeinflusst Primary nicht
- Stale Secondary macht Primary nicht stale
- Parser-State vollkommen getrennt

## Unterstützte NMEA-Sätze

| Satz | Status | Snapshot-Beitrag |
|------|--------|-----------------|
| GGA | **Pflicht** | Position, Fix, Satelliten, HDOP, Correction Age |
| RMC | **Pflicht** | Speed, Course, Motion-Validity |
| GST | **Pflicht** | Accuracy (std_lat/lon/alt) |
| GSA | Deferred | Geparsed, nicht gespeichert. Future Work. |
| GSV | Deferred | Geparsed, nicht gespeichert. Future Work. |

## Hard Rules (eingehalten)

- [x] **Keine direkte UART-I/O** in gnss_um980
- [x] **Kein PGN 214**
- [x] **Kein auto UM980-Config**
- [x] **Keine Steering-Änderungen**
- [x] **Keine RTCM3-Framevalidierung**
- [x] **Keine IMU-Fusion**
- [x] **Kein app_core-Monolith**

## Änderungen gegenüber NAV-GNSS-VALID-001 v1

| Aspekt | v1 | Nacharbeit |
|--------|----|-----------|
| Validität | `valid = GGA fix > 0` | Variante A: position_valid/motion_valid/accuracy_valid/valid/fresh |
| Freshness | Nur GGA-Zeitstempel | Per-Typ: GGA/RMC/GST getrennt |
| Checksum | Parse auch bei falscher Checksumme | **Kein Parse bei falscher Checksumme** |
| Fix Mapping | 0→NONE, 3→NONE | 3→PPS, 6+→UNKNOWN |
| Status Reason | Nicht vorhanden | 8 Reasons dokumentiert |
| Correction Age | 0.0 für leer | `correction_age_valid` Flag |
| Motion | Immer aus RMC | Nur bei Status A (motion_valid) |

## Dateien

| Datei | Änderung |
|-------|----------|
| `components/protocol_nmea/nmea_parser.h` | `age_diff_valid` zu nmea_gga_t |
| `components/protocol_nmea/nmea_parser.c` | Kein finalize bei bad checksum, age_diff_valid setzen |
| `components/gnss_snapshot/gnss_snapshot.h` | Komplett neu: Variant A, UNKNOWN fix, status_reason, correction_age |
| `components/gnss_snapshot/gnss_snapshot.c` | Komplett neu: per-type freshness, UNKNOWN mapping |
| `components/gnss_um980/gnss_um980.h` | Variant A Docs, has_fix → position_valid |
| `components/gnss_um980/gnss_um980.c` | Rebuild mit Variant A, correction_age, status_reason |
| `test/host/test_gnss_validation/test_gnss_validation.c` | Komplett neu: 40 Tests |
| `test/hardware/test_gnss_smoke/test_gnss_smoke.c` | Komplett neu: 5 Tests |
| `docs/hardware/nav-gnss-validation.md` | Dieses Dokument |
