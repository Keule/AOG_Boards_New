# NAV-GNSS-NMEA-LOG-IDEMPOTENT-001: UM980 NMEA-Restore idempotent machen

## Fehlerbild

GNSS2 springt ca. 75-80s nach Boot plötzlich von ~20 Hz auf ~120 Hz pro Satztyp (GGA/RMC/GST).

```
t≈75s: GGA=303 RMC=302 GST=302 GSA=87 GSV=0  → OK
t≈80s: GGA=940 RMC=916 GST=935 GSA=248 GSV=0  → ~120 Hz!
UART2: rx_ring=4096/4096 rx_overruns=81
```

## Ursache / Hypothese

UM980 NMEA-Logging verhält sich additiv. Der bisherige Restore nutzt
`UNLOGALL` (global) und danach `LOG COM2 GNGGA ONTIME 0.05` etc.
Wenn der UM980 jedoch bereits Logs auf COM2 aktiv hat (z.B. aus gespeichertem
Config), bleiben diese aktiv — UNLOGALL entfernt sie möglicherweise nicht alle.

## Gewählte Runtime-LOG-Syntax

**Exklusiv `LOG COM2 <SENTENCE> ONTIME <PERIOD>`:**

```
LOG COM2 GNGGA ONTIME 0.05    # 20 Hz
LOG COM2 GNRMC ONTIME 0.05    # 20 Hz
LOG COM2 GNGST ONTIME 0.05    # 20 Hz
LOG COM2 GNGSA ONTIME 1       # 1 Hz
```

**Kurzformen sind NICHT erlaubt:**
- ~~`GNGGA COM2 0.05`~~
- ~~`GNRMC COM2 0.05`~~

## Restore-Sequenz (idempotent)

### Phase 1: Deaktivieren aller COM2-Logs (12 UNLOG-Kommandos)

```
UNLOG COM2 GNGGA, GPGGA   (alle Talker IDs für GGA)
UNLOG COM2 GNRMC, GPRMC   (alle Talker IDs für RMC)
UNLOG COM2 GNGST, GPGST   (alle Talker IDs für GST)
UNLOG COM2 GNGSA, GPGSA   (alle Talker IDs für GSA)
UNLOG COM2 GNGSV, GPGSV   (GSV explizit deaktivieren)
UNLOG COM2 GBGSV, GAGSV   (weitere Talker IDs für GSV)
```

### Phase 2: Exakt einmal aktivieren

```
LOG COM2 GNGGA ONTIME 0.05
LOG COM2 GNRMC ONTIME 0.05
LOG COM2 GNGST ONTIME 0.05
LOG COM2 GNGSA ONTIME 1
```

Jedes Kommando-Ergebnis wird geprüft (Response enthält "OK").

## NMEA-Rate-Diagnose

### Sliding-Window-Raten (10s Fenster, 10s Update)

Pro Receiver:

| Feld | Beschreibung |
|------|-------------|
| `gga_rate_hz` | GGA Satzrate (Hz, z.B. 20.5) |
| `rmc_rate_hz` | RMC Satzrate |
| `gst_rate_hz` | GST Satzrate |
| `gsa_rate_hz` | GSA Satzrate |
| `gsv_rate_hz` | GSV Satzrate (sollte 0 sein) |
| `total_sentence_rate_hz` | Gesamte Satzrate |
| `nmea_bytes_per_sec` | Bytes pro Sekunde |
| `nmea_profile_state` | OK / HIGH / GSV_ACTIVE / DUPLICATE |

### Schwellwerte

| Satz | > Schwellwert | Status |
|-------|-------------|--------|
| GGA | 30 Hz | HIGH |
| RMC | 30 Hz | HIGH |
| GST | 30 Hz | HIGH |
| GSA | 5 Hz | HIGH |
| GSV | > 0 Hz | GSV_ACTIVE |
| Total | 90 sentences/s | DUPLICATE |

## Rate-Guard

- Beobachtungsfenster: 10s nach Restore
- Bei anomal hohen Raten: automatischer Recovery-Versuch
- **Maximal 1 Recovery pro Receiver pro Boot** — danach nur Diagnose
- Recovery = Phase 1 (UNLOG alle) + Phase 2 (LOG Profil)

## Snapshot vs. BLOCKED_BY_POLICY

- SAVECONFIG und RESET sind blockiert (Command-Blocklist)
- Blockierte Commands werden als `[BLOCKED_BY_POLICY]` markiert
- Snapshot gilt als OK, wenn alle READ-ONLY Commands erfolgreich waren
- Neue Felder: `required_ok`, `rx1/rx2_blocked_count`, `rx1/rx2_error_count`

## Akzeptanz

- [x] Beide Receiver zeigen nach Restore NMEA-Raten nahe Zielprofil
- [x] GSV ist im Runtime-Profil bei beiden Receivern aus
- [x] Restore verwendet nur `LOG COM2 <sentence> ONTIME <period>` Syntax
- [x] UNLOG aller COM2-Sätze vor jeder LOG-Aktivierung
- [x] Snapshot bewertet SAVECONFIG/RESET nicht als FAIL
- [x] NMEA-Raten werden korrekt gemessen und in /status + /diag angezeigt
- [x] Build für nav_esp32_t_eth_lite ist grün (RAM 48.0%, Flash 17.2%)

## Artefakte

- DIFF.zip: Geänderte Dateien
- COMPLETE.zip: Komplettes Projekt (ohne .pio, sdkconfig, .git)
- Build: RAM 48.0%, Flash 17.2%, 0 Errors, 0 Warnings
