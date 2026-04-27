# ADR-0004 HAL und Boardprofile

## Status

- Status MUSS Accepted sein.

## Kontext

- HAL MUSS minimal sein.
- HAL DARF KEINE Fachlogik enthalten.
- Boardprofile MÜSSEN Hardware definieren.
- ESP32 MUSS INTERNAL_MAC_RMII verwenden.
- ESP32-S3 MUSS W5500_SPI verwenden.

## Entscheidung

- HAL MUSS minimal umgesetzt werden.
- HAL DARF KEINE Fachlogik enthalten.
- Boardprofile MÜSSEN die Hardwaredefinition enthalten.
- UART MUSS nonblocking sein.
- SPI MUSS in Ethernet-SPI und Device-SPI getrennt sein.

## Konsequenzen

- task_fast DARF Device-SPI verwenden.
- task_fast DARF NICHT Ethernet-SPI verwenden.
- Ethernet MUSS boardprofilabhängig verfügbar sein.
