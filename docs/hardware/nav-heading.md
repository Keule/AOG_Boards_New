# NAV-HEADING-001: Dual-Antenna Heading (Nacharbeit)

## Status: NACHARBEIT ABGESCHLOSSEN

## Übersicht

Härtung der Dual-Antenna Heading-Komponente: **Strict Freshness, Timestamp-Synchronität, formelle Baseline-Tier-Qualität, dokumentierte Näherungsmethode**.

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
         │  Strict Freshness Check
         │  Timestamp Sync Check
         │  Baseline Plausibility
         │  Fix Quality Check
                ▼
        gnss_heading_snapshot_t
         ├─ heading_deg (0..360)
         ├─ valid / fresh
         ├─ quality (NONE/BAD/DEGRADED/GOOD/EXCELLENT)
         ├─ baseline_quality (INVALID/BAD/DEGRADED/GOOD)
         ├─ baseline_m (estimated)
         ├─ timestamp_delta_ms
         ├─ timestamp_ms
         ├─ reason (why invalid)
         └─ mounting_offset_deg (reserved)
                │
                ▼
        Downstream Consumers
         ├─ NAV-AOG-001: AOG Navigation App
         └─ Diagnostics / WebUI
```

## Antennenlayout

```
          ←── 70 cm ──→
    [Primary]         [Secondary]
       │                   │
       ▼                   ▼
    UM980 #0            UM980 #1
    (instance 0)        (instance 1)
```

### Montagehinweise

- Antennen sollten exakt 70 cm auseinander sein
- Antennen müssen gleiche Höhe haben (Horizontalmontage)
- Keine metallische Abschirmung zwischen Antennen
- Antennenkabel müssen sauber getrennt sein

## 70 cm Baseline

| Parameter | Wert |
|-----------|------|
| Erwartete Basislinie | 0.70 m |
| **GOOD** Bereich | 0.65 – 0.75 m (±7%) |
| **DEGRADED** Bereich | 0.50 – 0.90 m (±29%) |
| **BAD** Bereich | 0.10 – 0.50 m oder 0.90 – 2.00 m |
| **INVALID** | < 0.10 m oder > 2.00 m |
| Konfigurierbar | Ja (baseline_expected_m) |

### Baseline-Qualitätsstufen (Pflicht 1)

| Stufe | Bereich (bei 0.70m Soll) | Beschreibung |
|-------|--------------------------|-------------|
| `BASELINE_QUALITY_GOOD` | 0.65 – 0.75 m | Enge Toleranz, ±7% um Sollwert |
| `BASELINE_QUALITY_DEGRADED` | 0.50 – 0.90 m | Akzeptabel, ±29% |
| `BASELINE_QUALITY_BAD` | 0.10 – 0.50 m oder 0.90 – 2.00 m | Innerhalb absoluter Grenzen, aber weit ab |
| `BASELINE_QUALITY_INVALID` | < 0.10 m oder > 2.00 m | Außerhalb absoluter Grenzen |

## Primary/Secondary Richtung (Pflicht 5)

### Heading-Konvention

```
heading = bearing(primary → secondary)
```

- `heading = 0°` → Secondary ist **nördlich** von Primary
- `heading = 90°` → Secondary ist **östlich** von Primary
- `heading = 180°` → Secondary ist **südlich** von Primary
- `heading = 270°` → Secondary ist **westlich** von Primary

### ⚠️ WICHTIG: Antennenlinienheading ≠ Fahrzeugheading

Die berechnete Heading ist die **Antennenlinien-Heading**, NICHT automatisch die Fahrzeugheading.

```text
Fahrzeugheading = Antennenlinienheading + Montageoffset
```

### Montageoffset (Future Work)

- `mounting_offset_deg` ist im Snapshot reserviert (aktuell immer 0.0)
- Wird in einem zukünftigen Task konfigurierbar gemacht
- Installation-Profile ermöglichen verschiedene Fahrzeugtypen

## Näherungsmethode (Pflicht 4 — dokumentiert)

### Verwendete Methode: Lokale Equirectangular-Projektion

```text
dlat_m = (lat2 - lat1) × 111320.0
dlon_m = (lon2 - lon1) × 111320.0 × cos(avg_lat_rad)
distance = sqrt(dlat_m² + dlon_m²)
bearing = atan2(dlon_m, dlat_m) × (180/π)
```

### Gültigkeit

| Eigenschaft | Wert |
|-------------|------|
| **Gültig für** | Kurze Baseline (< 10 m) |
| **Bei 70 cm** | Winkelfehler < 0.01° (vernachlässigbar) |
| **Bei 1 m** | Winkelfehler < 0.02° |
| **Bei 10 m** | Winkelfehler < 0.2° |
| **Keine** | Geodätische Langstreckenberechnung |

### Bekannte Einschränkungen

- **Lokale Näherung**: Kein Vincenty/Geodesic-Algorithmus. Für 70 cm völlig ausreichend.
- **Keine Höhenkorrektur**: Altitudenunterschiede der Antennen werden ignoriert.
- **Keine Moving-Base RTK**: Heading wird aus Differenz zweier unabhängiger Positionen berechnet. UM980 Moving-Base ist nicht Teil dieses Tasks.
- **Keine IMU-Fusion**: Kein Gyro-/IMU-Input.
- **Spätere High-Precision-Geodesy**: Für längere Baselines (> 10 m) kann ein geodätischer Algorithmus (Vincenty) implementiert werden.

## Timestamp-Synchronität (Pflicht 2)

Heading wird **nicht** allein auf "beide fresh" basiert. Die Zeitstempel-Differenz wird explizit geprüft.

```text
timestamp_delta_ms = |primary.timestamp_ms - secondary.timestamp_ms|
```

| Delta | Wirkung | Beschreibung |
|-------|--------|-------------|
| ≤ 100 ms | Keine Qualitätsminderung | Snapshots sind synchronisiert |
| ≤ 250 ms | Quality auf **BAD** gedeckelt | Snapshots leicht asynchron — Bewegungsartefakte möglich |
| > 250 ms | **valid=false** | TIMESTAMP_MISMATCH — Heading nicht verwendbar |

### Ziel

Verhindern von Bewegungsartefakten bei zeitlich versetzten Snapshots. Wenn das Fahrzeug zwischen den zwei GNSS-Messungen bewegt wurde, ist die Heading unzuverlässig.

## Strict Freshness (Pflicht 3)

### Regel

Heading ist **nur gültig** wenn **alle** vier Bedingungen erfüllt sind:

```text
primary.position_valid  = true
secondary.position_valid = true
primary.fresh            = true
secondary.fresh          = true
```

### Nicht zulässig

```text
❌ primary position_valid + fresh, secondary position_valid + stale → valid
❌ primary position_valid + stale, secondary position_valid + fresh → valid
```

### Invalid-Reason Codes (Pflicht 3 — präzisiert)

| Code | Bedeutung |
|------|-----------|
| `PRIMARY_INVALID` | Primary NULL oder position_valid=false |
| `SECONDARY_INVALID` | Secondary NULL oder position_valid=false |
| `PRIMARY_STALE` | Primary position_valid=true aber fresh=false |
| `SECONDARY_STALE` | Secondary position_valid=true aber fresh=false |
| `TIMESTAMP_MISMATCH` | \|primary.timestamp - secondary.timestamp\| > 250 ms |
| `IDENTICAL_POS` | Beide Positionen identisch |
| `BASELINE_INVALID` | Baseline außerhalb absoluter Grenzen |
| `NO_FIX_PRIMARY` | Primary fix_quality = NONE oder UNKNOWN |
| `NO_FIX_SECONDARY` | Secondary fix_quality = NONE oder UNKNOWN |

## Heading-Qualitätsstufen

| Quality | Bedingungen | Beschreibung |
|---------|-------------|-------------|
| `EXCELLENT` | RTK_FIXED beide + baseline GOOD + timestamps synchronisiert | Beste Heading-Lösung |
| `GOOD` | Mindestens ein RTK + baseline ≥ DEGRADED + timestamps synchronisiert | Gute Heading-Lösung |
| `DEGRADED` | Brauchbarer Fix + baseline ≥ DEGRADED | Verwendbar, nicht ideal |
| `BAD` | Timestamp-Desync (100-250ms) ODER baseline BAD | Mit Vorsicht verwenden |
| `NONE` | Heading nicht berechenbar | Kein Heading verfügbar |

## Snapshot-Modell (Pflicht 7)

```c
typedef struct {
    bool valid;                          /* alle Checks bestanden */
    bool fresh;                          /* valid + alle Inputs fresh */
    double heading_deg;                  /* 0..360° */
    double baseline_m;                   /* geschätzte Distanz */
    gnss_heading_quality_t quality;      /* NONE/BAD/DEGRADED/GOOD/EXCELLENT */
    gnss_baseline_quality_t baseline_quality; /* INVALID/BAD/DEGRADED/GOOD */
    gnss_heading_reason_t reason;        /* warum invalid */
    uint64_t timestamp_ms;               /* Berechnungszeitstempel */
    uint64_t timestamp_delta_ms;         /* |primary.ts - secondary.ts| */
    uint32_t calc_count;                 /* erfolgreiche Berechnungen */
    double mounting_offset_deg;          /* reserviert (Future Work) */
} gnss_heading_snapshot_t;
```

### Keine unklare bool/float Mischform

- `quality`: klarer Enum (5 Stufen)
- `baseline_quality`: klarer Enum (4 Stufen)
- `valid`: bool (ja/nein)
- `fresh`: bool (ja/nein)
- `reason`: klarer Enum (10 Codes)

## Hard Rules (eingehalten)

- [x] **Keine UART-I/O** in gnss_dual_heading
- [x] **Kein PGN 214**
- [x] **Kein auto UM980-Config**
- [x] **Keine Steering-Änderungen**
- [x] **Keine IMU-Fusion**
- [x] **Kein app_core-Monolith**
- [x] **Keine RTCM-/NTRIP-Vermischung**
- [x] **Heading bleibt eigene Runtime-Komponente**
- [x] **task_fast bleibt deterministisch**
- [x] **ADR-Checks grün**

## ADR-Compliance

| Regel | Status |
|-------|--------|
| ADR-NO-PHYSICAL-IO-IN-TASK-FAST | N/A (nicht in task_fast) |
| ADR-TRANSPORT-NO-PROTOCOL-LOGIC | N/A (kein Transport) |
| ADR-AOG-APP-NO-PHYSICAL-UDP | N/A (keine AOG App) |
| ADR-GNSS-NO-DIRECT-UART | N/A (keine gnss_um980 I/O) |
| ADR-HEADING-NO-UART-NO-PGN | ✅ PASS |

## Änderungen gegenüber NAV-HEADING-001 v1

| Aspekt | v1 | Nacharbeit |
|--------|----|-----------|
| Baseline-Toleranz | Einziger tolerance_pct (30%) | 3 explizite Tiers: GOOD/DEGRADED/BAD/INVALID |
| Freshness | Stale → valid=true, fresh=false | **STRICT**: Stale → valid=false (PRIMARY_STALE/SECONDARY_STALE) |
| Timestamp-Sync | Nicht geprüft | Delta geprüft: ≤100ms GOOD, ≤250ms BAD, >250ms invalid |
| Baseline-Qualität | Nur in quality eingebettet | Eigener Enum `baseline_quality` |
| Reason-Codes | 10 Codes (gemischt INVALID/STALE) | 10 präzise Codes (INVALID vs STALE getrennt) |
| Näherungsmethode | Nicht explizit dokumentiert | Vollständig dokumentiert mit Fehlergrenzen |
| Montageoffset | Nicht erwähnt | Dokumentiert als Future Work, Feld reserviert |
| Quality-BAD | Nicht explizit als Stufe | Neue Stufe zwischen DEGRADED und NONE |
| `set_baseline_tolerance()` | vorhanden | **entfernt** (durch feste Tiers ersetzt) |
| `classify_baseline()` | nicht vorhanden | Neue Utility-Funktion (exposed für Tests) |

## API-Änderungen (Breaking)

- **ENTFERNT**: `gnss_dual_heading_set_baseline_tolerance()` (durch feste Tiers ersetzt)
- **NEU**: `gnss_dual_heading_classify_baseline()` (Utility für Tests)
- **GEÄNDERT**: `gnss_heading_reason_t` — `NO_PRIMARY` → `PRIMARY_INVALID`, `NO_SECONDARY` → `SECONDARY_INVALID`
- **GEÄNDERT**: `gnss_heading_reason_t` — `BASELINE_TOO_SMALL`/`TOO_LARGE` → `BASELINE_INVALID`
- **NEU**: `gnss_baseline_quality_t` Enum
- **NEU**: `heading.timestamp_delta_ms` Feld
- **NEU**: `heading.baseline_quality` Feld
- **NEU**: `heading.mounting_offset_deg` Feld (reserviert)
- **GEÄNDERT**: `HEADING_QUALITY_BAD` neue Stufe (zwischen DEGRADED und NONE)

## Test-Matrix

| # | Test | Beschreibung |
|---|------|-------------|
| 1 | Init | Alle Felder Zero/Invalid, mounting_offset=0.0 |
| 2-6 | Bearing N/E/S/W/SW | Bekannte Richtungen + Normalisierung |
| 7 | Distance | Bekannte Basislinie |
| 8 | Valid both fresh | position_valid + fresh auf beiden → valid |
| 9-10 | NULL primary/secondary | INVALID reason |
| 11-12 | Not position_valid | INVALID reason |
| 13 | **PRIMARY_STALE** | **Pflicht 3: stale → valid=false** |
| 14 | **SECONDARY_STALE** | **Pflicht 3: stale → valid=false** |
| 15 | Both stale | PRIMARY_STALE (erster Check) |
| 16 | Identical positions | IDENTICAL_POS |
| 17 | **Baseline near-zero** | **BASELINE_INVALID** |
| 18 | Baseline too large | BASELINE_INVALID |
| 19 | **Baseline 0.70m** | **GOOD quality** |
| 20-21 | **Baseline 0.65/0.75m** | **GOOD boundaries** |
| 22-23 | **Baseline 0.50/0.90m** | **DEGRADED boundaries** |
| 24-25 | **Baseline 0.40/1.50m** | **BAD quality** |
| 26-27 | Classification min/max | INVALID |
| 28 | DEGRADED+SINGLE | Quality DEGRADED |
| 29 | BAD+RTK | Quality BAD (baseline caps) |
| 30 | EXCELLENT all | RTK_FIXED both + GOOD + sync |
| 31 | GOOD one RTK | RTK one + GOOD baseline |
| 32 | **Timestamp desync 150ms** | **Quality BAD** |
| 33 | **Timestamp mismatch 300ms** | **valid=false** |
| 34-36 | **Timestamp thresholds** | **100ms=sync, 250ms=valid/BAD, 0ms=perfect** |
| 37 | **Swapped antennas** | **~180° offset** |
| 38 | **359° ↔ 0° wrap** | **Korrekte Normalisierung** |
| 39-40 | Fix NONE/UNKNOWN | INVALID |
| 41-42 | Counters/timestamps | Inkrement und Weitergabe |
| 43 | Null safety | Alle API-Funktionen |
| 44-46 | Service step/is_fresh/getter | Delegation und Zugriff |
| 47-50 | 70cm N/E/S/W | Kardinalrichtungen |
| 51 | **Freshness transition** | **Both fresh → one stale → valid=false** |

**Gesamt: 51 Tests**

## Dateien

| Datei | Änderung |
|-------|----------|
| `components/gnss_dual_heading/gnss_dual_heading.h` | Komplett neu: Baseline-Tiers, Timestamp-Sync, Strict-Freshness, baseline_quality |
| `components/gnss_dual_heading/gnss_dual_heading.c` | Komplett neu: 3-Tier-Baseline, Timestamp-Delta-Check, Strict-Freshness-Chain |
| `test/host/test_heading_validation/test_heading_validation.c` | Komplett neu: 51 Tests |
| `docs/hardware/nav-heading.md` | Komplett neu: alle Pflichtinhalte |
