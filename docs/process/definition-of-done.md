# Definition of Done (DoD)

## 1) Allgemeine Tasks
Eine Aufgabe gilt nur als "Done", wenn:
- alle geforderten Dateien vorhanden sind,
- alle geforderten Inhalte im Sollzustand sind,
- alle Akzeptanzkriterien erfüllt sind,
- keine verbotenen Änderungen enthalten sind.

## 2) Build-/Skeleton-Tasks
Zusätzlich gilt:
- Build muss funktionieren, **wenn Code betroffen ist**.
- Keine Überimplementierung (nur Skeleton/Stub, wenn gefordert).
- Keine verbotenen Includes.
- Keine Layer-Verletzungen.

## 3) Architektur-/Dokumentations-Tasks
Zusätzlich gilt:
- Dokumente sind konsistent mit bestehenden Regeln (`AGENTS.md`, `agents/*`, ADRs).
- Architekturentscheidungen werden nicht stillschweigend geändert.
- Regeln sind klar, verbindlich und prüfbar formuliert.

## 4) Code-/Runtime-Tasks
Zusätzlich gilt:
- CMake-/Dependency-Angaben sind vollständig.
- Keine Layer-Verletzungen (z. B. Protokoll ↔ HAL).
- Keine verbotenen Includes.
- Keine ungeforderte Zusatzlogik.

## 5) Review-/Nacharbeits-Tasks
Zusätzlich gilt:
- Review entscheidet eindeutig: **ACCEPTED** oder **REJECTED**.
- Bei **REJECTED** sind Root Cause und Lessons Learned benannt.
- Nacharbeit bleibt klein, konkret und scope-treu.
- Nacharbeit behebt nur benannte Mängel.
