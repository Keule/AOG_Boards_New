# NAV-HEADING-001: Dual-Antenna Heading from Primary/Secondary GNSS

## Status: IMPLEMENTIERT

## Übersicht

Berechnet Heading aus zwei GNSS-Positionen (Primary + Secondary UM980).
Verwendet lokale Flat-Earth-Näherung für die 70 cm Antennenbasis.

## Architektur

```
Primary UM980                Secondary UM980
     │                              │
     ▼                              ▼
gnss_um980 (instance 0)     gnss_um980 (instance 1)
     │                              │
     ▼                              ▼
gnss_snapshot_t (primary)   gnss_snapshot_t (secondary)
     │                              │
     └──────────┬───────────────────┘
                ▼
        gnss_dual_heading_calc
                │
                ▼
        gnss_heading_snapshot_t
         ├─ heading_deg (0..360)
         ├─ baseline_m (estimated)
         ├─ valid / fresh
         ├─ quality (NONE..EXCELLENT)
         └─ reason (why invalid)
                │
                ▼
        Downstream Consumers
         ├─ NAV-AOG-001: AOG Navigation App
         └─ Diagnostics / WebUI
```

## Antennenmontage

```
          ←── 70 cm ──→
    [Primary]         [Secondary]
       │                   │
       ▼                   ▼
    UM980 #0            UM980 #1
    (instance 0)        (instance 1)

Richtung: Primary → Secondary definiert die Heading-Achse.
Heading = 0° → Secondary ist nördlich von Primary.
Heading = 90° → Secondary ist östlich von Primary.
```

### Montagehinweise

- Antennen sollten exakt 70 cm auseinander sein (±5% empfohlen)
- Antennen müssen gleiche Höhe haben (Horizontalmontage)
- Keine metallische Abschirmung zwischen Antennen
- Antennenkabel müssen sauber getrennt sein (kein Übersprechen)

## 70 cm Baseline

| Parameter | Wert |
|-----------|------|
| Erwartete Basislinie | 0.70 m |
| Toleranzband | ±30% (default) |
| Absolutes Minimum | 0.10 m |
| Absolutes Maximum | 2.00 m |
| Konfigurierbar | Ja (baseline_expected_m, baseline_tolerance_pct) |

### Basislinien-Plausibilität

Die geschätzte Basislinie wird gegen den erwarteten Wert geprüft:
- **Im Toleranzband** (0.49m .. 0.91m bei default 30%): Qualität EXCELLENT/GOOD
- **Außerhalb**: Qualität DEGRADED/POOR
- **Unter 0.10m**: heading invalid (BASELINE_TOO_SMALL)
- **Über 2.00m**: heading invalid (BASELINE_TOO_LARGE)

## Richtung primary → secondary

Die Heading wird als Bearing von Primary zu Secondary berechnet:
- `bearing = atan2(east_displacement_m, north_displacement_m)`
- Ergebnis: 0° = Nord, 90° = Ost, 180° = Süd, 270° = West
- Normalisierung: immer [0, 360)

## Berechnung (Flat-Earth)

### Distanz
```
dlat_m = (lat2 - lat1) × 111320.0
dlon_m = (lon2 - lon1) × 111320.0 × cos(avg_lat_rad)
distance = sqrt(dlat_m² + dlon_m²)
```

### Heading
```
heading = atan2(dlon_m, dlat_m) × (180/π)
if heading < 0: heading += 360
```

### Bekannte Einschränkungen

- **Lokale Flat-Earth-Näherung**: Für 70 cm Basislinie ist der Fehler < 0.01° und somit vernachlässigbar. Für Basislinien > 10 m sollte eine geodätisch exakte Berechnung (Vincenty) verwendet werden.
- **Keine Moving-Base Konfiguration**: Die Heading-Berechnung basiert auf der Differenz zweier unabhängiger GNSS-Positionen. UM980 Moving-Base RTK ist nicht Teil dieses Tasks.
- **Keine IMU-Fusion**: Die Heading wird ausschließlich aus GNSS-Positionen berechnet. Kein Gyro-/IMU-Input.

## Plausibilitätsregeln

Ein Heading ist **nur gültig** wenn:

| # | Regel | Invalid-Reason |
|---|-------|---------------|
| 1 | Primary-Snapshot != NULL und `position_valid` | `NO_PRIMARY` |
| 2 | Secondary-Snapshot != NULL und `position_valid` | `NO_SECONDARY` |
| 3 | Primary fix_quality != NONE und != UNKNOWN | `NO_FIX_PRIMARY` |
| 4 | Secondary fix_quality != NONE und != UNKNOWN | `NO_FIX_SECONDARY` |
| 5 | Positionen nicht identisch (dlat != 0 oder dlon != 0) | `IDENTICAL_POS` |
| 6 | Basislinie >= 0.10 m | `BASELINE_TOO_SMALL` |
| 7 | Basislinie <= 2.00 m | `BASELINE_TOO_LARGE` |

## Freshness

Ein Heading ist **fresh** wenn:
- `heading.valid == true`
- **Beide** Input-Snapshots `fresh == true`
- Eigener Freshness-Timeout nicht abgelaufen (default: 2000 ms)

| Zustand | valid | fresh | Bedeutung |
|---------|-------|-------|-----------|
| Beide Snapshots fresh + alle Plausibilitäten OK | ✓ | ✓ | Normale Heading verfügbar |
| Primary stale, Secondary fresh | ✓ | ✗ | Heading berechenbar, aber veraltet |
| Primary invalid | ✗ | ✗ | Kein Heading |

## Heading Quality

| Quality | Bedingung | Beschreibung |
|---------|-----------|-------------|
| `EXCELLENT` | RTK_FIXED auf beiden + Basislinie im Toleranzband | Beste Heading-Lösung |
| `GOOD` | RTK (Fixed oder Float) + Basislinie im Toleranzband | Gute Heading-Lösung |
| `DEGRADED` | SINGLE/DGPS/PPS fix + Basislinie im Toleranzband | Verwendbar, nicht ideal |
| `POOR` | Basislinie deutlich abweichend oder keine RTK-Fix-Qualität | Heading mit Vorsicht verwenden |
| `NONE` | Heading nicht berechenbar | Kein Heading verfügbar |

## Status Reason Codes

| Code | Bedeutung |
|------|-----------|
| `HEADING_REASON_NONE` | Alles OK |
| `HEADING_REASON_NO_PRIMARY` | Primary NULL oder nicht position_valid |
| `HEADING_REASON_NO_SECONDARY` | Secondary NULL oder nicht position_valid |
| `HEADING_REASON_PRIMARY_STALE` | Primary nicht fresh |
| `HEADING_REASON_SECONDARY_STALE` | Secondary nicht fresh |
| `HEADING_REASON_IDENTICAL_POS` | Beide Positionen identisch |
| `HEADING_REASON_BASELINE_TOO_SMALL` | Basislinie < 0.10 m |
| `HEADING_REASON_BASELINE_TOO_LARGE` | Basislinie > 2.00 m |
| `HEADING_REASON_NO_FIX_PRIMARY` | Primary hat keinen Fix (NONE/UNKNOWN) |
| `HEADING_REASON_NO_FIX_SECONDARY` | Secondary hat keinen Fix (NONE/UNKNOWN) |

## API

```c
/* Init */
void gnss_dual_heading_init(gnss_dual_heading_calc_t* calc);

/* Set sources */
void gnss_dual_heading_set_sources(calc, primary_snap, secondary_snap);

/* Configure */
void gnss_dual_heading_set_baseline(calc, 0.70);
void gnss_dual_heading_set_baseline_tolerance(calc, 30.0);
void gnss_dual_heading_set_freshness_timeout(calc, 2000);

/* Calculate (manual / service_step) */
void gnss_dual_heading_calculate(calc, current_ms);
void gnss_dual_heading_service_step(comp, timestamp_us);

/* Get results */
const gnss_heading_snapshot_t* gnss_dual_heading_get(calc);
bool gnss_dual_heading_is_fresh(calc);

/* Utilities */
double gnss_dual_heading_bearing(lat1, lon1, lat2, lon2);
double gnss_dual_heading_distance_m(lat1, lon1, lat2, lon2);
```

## Hard Rules (eingehalten)

- [x] **Keine UART-I/O** in gnss_dual_heading
- [x] **Kein PGN 214**
- [x] **Kein auto UM980-Config**
- [x] **Keine Steering-Änderungen**
- [x] **Keine IMU-Fusion**
- [x] **Kein app_core-Monolith**
- [x] **ADR-HEADING-NO-UART-NO-PGN** Regel erstellt

## ADR-Compliance

| Regel | Status |
|-------|--------|
| ADR-NO-PHYSICAL-IO-IN-TASK-FAST | N/A (nicht in task_fast) |
| ADR-TRANSPORT-NO-PROTOCOL-LOGIC | N/A (kein Transport) |
| ADR-AOG-APP-NO-PHYSICAL-UDP | N/A (keine AOG App) |
| ADR-GNSS-NO-DIRECT-UART | N/A (keine gnss_um980 I/O) |
| ADR-HEADING-NO-UART-NO-PGN | ✅ Neu erstellt |

## Test-Matrix

| # | Test | Beschreibung |
|---|------|-------------|
| 1 | Init | Alle Felder Zero/Invalid |
| 2-4 | Bearing N/E/S/W | Bekannte Richtungen |
| 5 | Normalization | Negative Winkel → [0,360) |
| 6-7 | Distance | Bekannte Basislinie |
| 8 | Valid both fresh | Beide Snapshots gültig |
| 9-10 | NULL primary/secondary | Invalid |
| 11-12 | Not position_valid | Invalid |
| 13-14 | Stale (not fresh) | Valid aber not fresh |
| 15 | Identical positions | Invalid |
| 16-17 | Baseline min/max | Invalid |
| 18-21 | Quality E/G/D/P | Alle Qualitätsstufen |
| 22-24 | Fix NONE/UNKNOWN | Invalid |
| 25-28 | Configuration | Baseline, Tolerance, Timeout |
| 29-30 | Counter/Timestamp | Inkrement und Weitergabe |
| 31 | Null safety | Alle API-Funktionen |
| 32 | service_step | Delegation an calculate |
| 33 | is_fresh | Hilfsfunktion |
| 34 | Getter | Pointer-Zurückgabe |
| 35-37 | 70cm N/E/S/W | Echte 70cm Kardinalrichtungen |
| 38-42 | Additional | DGPS, PPS, Counter, Freshness, Preserve |

**Gesamt: 42 Tests**

## Dateien

| Datei | Änderung |
|-------|----------|
| `components/gnss_dual_heading/gnss_dual_heading.h` | Komplett neu: Heading Snapshot, Quality, Reason, Calculator |
| `components/gnss_dual_heading/gnss_dual_heading.c` | Komplett neu: Bearing, Plausibilität, Freshness |
| `components/gnss_dual_heading/CMakeLists.txt` | REQUIRES gnss_snapshot runtime_components |
| `extra_scripts/native_test.py` | gnss_dual_heading hinzugefügt |
| `tools/adr_checks/adr_rules.yaml` | ADR-HEADING-NO-UART-NO-PGN hinzugefügt |
| `test/host/test_heading_validation/test_heading_validation.c` | 42 Host-Tests |
| `docs/hardware/nav-heading.md` | Dieses Dokument |
