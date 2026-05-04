# NAV-UART Hardware-Spezifikation

## GNSS-Empfänger (UM980)

- Zwei UM980-Empfänger, jeder auf eigenem UART.
- Baudrate: 921600 (non-blocking).
- UART-Treiber: HAL-UART mit ops-Pattern (ESP32/stub).

## Buffergrößen

| Parameter         | Wert | Begründung                                                       |
|-------------------|------|------------------------------------------------------------------|
| RX-Buffer         | 4096 | Bei 921600 baud (~12 KB/s GNSS-Datenstrom). 4096 Bytes decken ~340 ms  |
|                   |      | ab und verhindern Overflows auch bei Busy-Phasen.                 |
|                   |      | (War 1024, erhöht wegen Overflow-Problemen bei 921600 baud.)     |
| TX-Buffer         | 1024 | RTCM-Korrekturdaten können Burst-artig ankommen.                 |
|                   |      | 1024 Bytes deckt mehrere RTCM3-Nachrichten ab.                    |
|                   |      | (War 512, erhöht für RTCM-Burst-Sicherheit.)                     |
| RX/TX-Burstgröße  | 128  | Bytes pro service_step Lese-/Schreib-Batch.                      |
|                   |      | 128 Bytes passen komfortabel in den Stack und minimieren         |
|                   |      | die Anzahl der HAL-Aufrufe pro Zyklus.                           |

## Abgleich mit Code

Diese Werte MÜSSEN mit den Defines in `transport_uart.h` übereinstimmen:

```c
#define TRANSPORT_UART_RX_BUFFER_SIZE  4096
#define TRANSPORT_UART_TX_BUFFER_SIZE  1024
#define TRANSPORT_UART_BURST_SIZE      128
```

Die HAL-UART Default-Fallback-Puffer (wenn `rx_buffer_size`/`tx_buffer_size` = 0)
in `hal_uart_esp32.c` MÜSSEN ebenfalls diese Werte verwenden.
