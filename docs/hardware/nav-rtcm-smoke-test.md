# NAV-RTCM-001 Hardware Smoke Test — Dual UM980

## Zweck
Prüfen, dass beide angeschlossenen UM980 tatsächlich denselben RTCM-Stream
über UART empfangen und korrekt verarbeiten.

## Voraussetzungen

1. **Hardware:**
   - NAV-Controller (LilyGO T-ETH Lite ESP32 oder ESP32-S3)
   - Zwei UM980 GNSS-Empfänger angeschlossen
   - Ethernet-Kabel verbunden
   - Beide GNSS-UART-TX-Pfade verdrahtet:
     - ESP32: GPIO14 ↔ UM980 #1 RXIN, GPIO15 ↔ UM980 #2 RXIN
     - ESP32-S3: GPIO6 ↔ UM980 #1 RXIN, GPIO7 ↔ UM980 #2 RXIN
   - Beide UM980 RX-Pfade verdrahtet:
     - ESP32: GPIO16 ↔ UM980 #1 TX, GPIO17 ↔ UM980 #2 TX

2. **Konfiguration:**
   - NTRIP-Konfiguration gültig (Host, Mountpoint, User/Pass)
   - NTRIP-Caster erreichbar und liefert RTCM3-Daten
   - Firmware mit NAV-RTCM-001 Nacharbeit geflasht

## Ablauf

### Schritt 1: Boot und Init-Log prüfen

```
I (xxxx) APP_CORE: System init
I (xxxx) APP_CORE: Initializing navigation subsystem
I (xxxx) APP_CORE: RTCM router: 2 output(s) wired for 2 active GNSS receiver(s)
```

**Erwartung:**
- `"2 output(s) wired for 2 active GNSS receiver(s)"` → produktiv
- Kein `"NAV-RTCM-001 not productive"` Error
- Kein `"has no TX pin"` Warning

### Schritt 2: NTRIP-Verbindung prüfen

```
I (xxxx) NTRIP: Connected to ntrip.example.com:2101
I (xxxx) NTRIP: Receiving RTCM data
```

**Erwartung:**
- NTRIP connected
- RTCM-Datenfluss beginnt

### Schritt 3: RTCM-Router Stats prüfen

Über serielle Konsole oder Diagnose-Output:

| Feld | Erwartung |
|------|-----------|
| `rtcm bytes_in` | Steigt kontinuierlich (NTRIP liefert Daten) |
| `bytes_out` (pro Output) | Steigt proportional zu `bytes_in` |
| `bytes_dropped` (pro Output) | 0 oder sehr niedrig |
| `last_activity_us` | Aktuell (nicht stale) |
| `output_overflow_count` | 0 im Normalbetrieb |

**Guter Indikator:**
```
bytes_in ≈ bytes_out(Output1) ≈ bytes_out(Output2)
```

### Schritt 4: UM980 Korrekturstatus prüfen

Über UM980-NMEA-Output oder Diagnose:
- Beide UM980 sollten RTK-Fix erreichen
- `GGA`-Sentence: Fix-Qualität = 4 (RTK Fixed) oder 5 (RTK Float)
- Beide Empfänger zeitgleich im Fix

### Schritt 5: Langzeit-Stabilität (5+ Minuten)

- `bytes_dropped` bleibt niedrig (< 0.1% von `bytes_in`)
- Keine `output_overflow_count`-Erhöhungen
- Keine NTRIP-Disconnects
- Beide UM980 bleiben im RTK-Fix

## Diagnosedaten auslesen

### Per-Output Stats

```
Output 0 (GNSS_PRIMARY):
  bytes_forwarded: <steigend>
  bytes_dropped:   0

Output 1 (GNSS_SECONDARY):
  bytes_forwarded: <steigend>
  bytes_dropped:   0
```

### Transport UART TX-Stats

```
UART GNSS_PRIMARY:
  tx_bytes_out: <steigend>
  tx_errors:     0
  tx_overflows:  0

UART GNSS_SECONDARY:
  tx_bytes_out: <steigend>
  tx_errors:     0
  tx_overflows:  0
```

## Fehlerfälle

### Fall 1: Ein Empfänger getrennt

**Symptom:** Ein UART-TX-Zähler steigt, der andere nicht.
**Diagnose:** Kabelverbindung oder UM980-Stromversorgung prüfen.
**Erwartetes Verhalten:** Router verteilt weiterhin an beide Outputs.
Der getrennte UART TX-Buffer läuft voll → `bytes_dropped` steigt.
Der andere Empfänger ist **unbeeinträchtigt** (Drop-Policy).

### Fall 2: Ein UART-TX-Pfad fehlt (Hardware-Defekt)

**Symptom:** Boot-Log zeigt:
```
E (xxxx) APP_CORE: NAV-RTCM-001 not productive: active GNSS receiver without RTCM TX path
```
**Erwartetes Verhalten:** Navigation init bricht ab. System startet nicht produktiv.
**Diagnose:** Boardprofil prüfen. TX-Pin Verdrahtung prüfen.

### Fall 3: Output-Buffer voll

**Symptom:** `bytes_dropped` steigt, `output_overflow_count` steigt.
**Ursache:** UART-Baudrate zu niedrig oder NTRIP-Datenrate zu hoch.
**Behobung:** UART-Baudrate prüfen (sollte 921600 sein).
Buffer-Größen ggf. erhöhen.

### Fall 4: NTRIP disconnected

**Symptom:** `bytes_in` steigt nicht mehr. `last_activity_us` wird stale.
**Erwartetes Verhalten:** Router liest 0 Bytes, verteilt nichts.
Keine Drops, kein Fehler. Nach Reconnect fließt RTCM automatisch weiter.
**Diagnose:** NTRIP-Caster Erreichbarkeit, Credentials prüfen.

### Fall 5: RTCM_ROUTER_MAX_OUTPUTS überschritten

**Symptom:** Boot-Log zeigt:
```
E (xxxx) APP_CORE: NAV-RTCM-001 not productive: rtcm_router_add_output() failed
(router capacity 2 exceeded by active GNSS receivers)
```
**Ursache:** Boardprofil hat mehr aktive GNSS-Empfänger als Router-Outputs.
**Behobung:** `RTCM_ROUTER_MAX_OUTPUTS` in `rtcm_router.h` erhöhen.
**Aktuell:** Max 2 Outputs. Bei 3+ Empfängern muss die Konstante angepasst werden.
