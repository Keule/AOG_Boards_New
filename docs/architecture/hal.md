# HAL

- HAL MUSS minimal sein.
- HAL DARF KEINE Fachlogik enthalten.
- Boardprofile MÜSSEN Hardware definieren.
- ESP32 MUSS INTERNAL_MAC_RMII verwenden.
- ESP32-S3 MUSS W5500_SPI verwenden.
- UART MUSS nonblocking sein.
- SPI MUSS getrennt sein:
  - Ethernet-SPI
  - Device-SPI
- task_fast DARF Device-SPI verwenden.
- task_fast DARF NICHT Ethernet-SPI verwenden.
