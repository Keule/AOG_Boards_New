# ADR-0005 Teststrategie

## Status

- Status MUSS Accepted sein.

## Kontext

- Das System MUSS test-first entwickelt werden.
- Logik MUSS ohne Hardware testbar sein.
- Simulatoren MÜSSEN existieren.
- Simulatoren DÜRFEN NICHT Teil der Runtime sein.

## Entscheidung

- Entwicklung MUSS test-first erfolgen.
- Logik MUSS hardwareunabhängig testbar sein.
- Simulatoren MÜSSEN bereitgestellt werden.
- Simulatoren DÜRFEN NICHT in Runtime eingebunden sein.

## Konsequenzen

- Tests MÜSSEN ohne Zielhardware ausführbar sein.
- Runtime MUSS ohne Simulatorabhängigkeiten laufen.
