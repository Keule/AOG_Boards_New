# ADR-0003 Layer-Trennung

## Status

- Status MUSS Accepted sein.

## Kontext

- Das System MUSS HAL, Transport, Protokoll, App und Hardwaremodule verwenden.
- Transport DARF KEINE Fachlogik enthalten.
- Protokoll DARF KEINEN Hardwarezugriff enthalten.
- HAL DARF KEINE Fachlogik enthalten.

## Entscheidung

- Das System MUSS die Schichten HAL, Transport, Protokoll, App und Hardwaremodule verwenden.
- Transport DARF KEINE Fachlogik enthalten.
- Protokoll DARF KEINEN Hardwarezugriff enthalten.
- HAL DARF KEINE Fachlogik enthalten.

## Konsequenzen

- Fachlogik MUSS außerhalb von HAL und Transport liegen.
- Hardwarezugriff MUSS außerhalb der Protokollschicht liegen.
