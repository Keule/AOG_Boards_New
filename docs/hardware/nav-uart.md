# NAV-UART Hardware-Spezifikation

## GNSS-Empfänger (UM980)

- Zwei UM980-Empfänger, jeder auf eigenem UART.
- Baudrate: 921600 (non-blocking).
- UART-Treiber: HAL-UART mit ops-Pattern (ESP32/stub).

## Buffergrößen

| Parameter         | Wert | Begründung                                                       |
|-------------------|------|------------------------------------------------------------------|
| RX-Buffer         | 1024 | Bei 921600 baud (~10.8 µs/Byte) reicht 256 Bytes für ~2.8 ms.   |
|                   |      | Mit 1024 Bytes werden ~11 ms abgedeckt, was den 10 ms           |
|                   |      | Service-Zyklus sicher überbrückt und Overflows verhindert.       |
| TX-Buffer         | 512  | RTCM-Korrekturdaten können Burst-artig ankommen.                 |
|                   |      | 512 Bytes deckt mehrere RTCM3-Nachrichten ab.                    |
| RX/TX-Burstgröße  | 128  | Bytes pro service_step Lese-/Schreib-Batch.                      |
|                   |      | 128 Bytes passen komfortabel in den Stack und minimieren         |
|                   |      | die Anzahl der HAL-Aufrufe pro Zyklus.                           |

## Abgleich mit Code

Diese Werte MÜSSEN mit den Defines in `transport_uart.h` übereinstimmen:

```c
#define TRANSPORT_UART_RX_BUFFER_SIZE  1024
#define TRANSPORT_UART_TX_BUFFER_SIZE  512
#define TRANSPORT_UART_BURST_SIZE      128
```

Die HAL-UART Default-Fallback-Puffer (wenn `rx_buffer_size`/`tx_buffer_size` = 0)
in `hal_uart_esp32.c` MÜSSEN ebenfalls diese Werte verwenden.
