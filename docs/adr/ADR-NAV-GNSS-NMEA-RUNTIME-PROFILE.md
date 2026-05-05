# ADR: NAV-GNSS-NMEA-Runtime-Profile

## Status: ACCEPTED

## Context

Die NAV-GNSS-NMEA-LOG-IDEMPOTENT-001 Aufgabe identifizierte, dass
GNSS2 nach Boot/Snapshot/Restore plötzlich ~120 Hz pro Satztyp sendete statt
der erwarteten ~20 Hz. Ursache: additive UM980-Logs bei nicht-idempotentem Restore.

## Entscheidung

### 1. Runtime-Profil verwendet ausschließlich eine Syntax

**AKZEPTIERT:** Nur `LOG COM2 <sentence> ONTIME <period>`

Rationale:
- Einheitliche Syntax vermeidet Verwirrung zwischen Boot-Phase und Runtime-Restore
- Vollständige Kontrolle über aktivierte Logs
- Kurzform (z.B. `GNGGA COM2 0.05`) wird **nicht** verwendet

### 2. Vor Restore wird COM2 idempotent bereinigt

**AKZEPTIERT:** Phase 1 deaktiviert alle bekannten COM2-Logs vor Phase 2.

Rationale:
- UM980 kann Logs additiv aktivieren (mehrere LOG für denselben Satz)
- UNLOGALL (global) reicht allein nicht aus — targeting UNLOG pro Satz/Talker-ID
- 12 UNLOG-Kommandes decken alle Talker-IDs (GN, GP, GA, GB)

### 3. GSV ist im Produktivprofil deaktiviert

**AKZEPTIERT:** GSV wird explizit mit 4 UNLOG-Kommandos deaktiviert.

Rationale:
- GSV verursacht ~40-60% des NMEA-Traffics ohne Navigationsnutzen
- GSV-Sätze überschwemmen UART-RX-Puffer bei hohlem Multiplex-Betrieb

### 4. Snapshot und Runtime-Restore sind getrennte Phasen

**AKZEPTIERT:**

1. Snapshot start (settling)
2. Optional UNLOG für Command-Antworten
3. Query VERSIONA / CONFIG / MODE / MASK
4. Snapshot-Ergebnis bewerten (read-only Commands)
5. Runtime-NMEA-Profil idempotent wiederherstellen (Phase 1 + Phase 2)
6. Rate-Guard starten

### 5. Blockierte Persistenz-/Reset-Kommandos sind keine Snapshot-Fehler

**AKZEPTIERT:** SAVECONFIG, RESET, FRESET, UPGRADE sind in der Blocklist.

- Snapshot gilt als OK, wenn alle READ-ONLY Commands erfolgreich waren
- Blockierte Commands werden als `[BLOCKED_BY_POLICY]` markiert, nicht als FAIL
- Neue Felder: `required_ok`, `blocked_count`, `error_count`, `timeout_count`

## Konsequenzen

- Restore-Sequenz dauert länger (~2-3s statt ~1s wegen 12 UNLOG + 4 LOG)
- Erhöhte Robustheit: keine additiven Logs mehr
- Diagnosefähigkeit: Rate-Anomalien werden sofort erkannt und geloggt
- Rate-Guard: automatischer Recovery bei erkannter Überlast (1x pro Boot max)
