# ADR-0001 Hard-Cut ESP-IDF

## Status

- Status MUSS Accepted sein.

## Kontext

- Das System MUSS ausschließlich ESP-IDF verwenden.
- Das System DARF KEINE Arduino-Komponenten enthalten.
- Das Buildsystem MUSS PlatformIO sein.

## Entscheidung

- Das System MUSS ausschließlich mit ESP-IDF umgesetzt werden.
- Das System DARF KEINE Arduino-Komponenten enthalten.
- Das Buildsystem MUSS PlatformIO sein.

## Konsequenzen

- Alle Komponenten MÜSSEN ESP-IDF-konform sein.
- Alle Builds MÜSSEN PlatformIO verwenden.
- Arduino-Abhängigkeiten DÜRFEN NICHT enthalten sein.
