# Datenfluss

- Das System DARF KEINEN globalen Datenbus verwenden.
- Das System MUSS Ports und Buffer pro Komponente verwenden.
- Das System MUSS folgende Strukturen verwenden:
  - ByteRingBuffer
  - MessageQueue<T>
  - SnapshotBuffer<T>
- Das System MUSS einen FastCycleContext verwenden.
- FastCycleContext MUSS cycle_id enthalten.
- FastCycleContext MUSS timestamp enthalten.
- FastCycleContext MUSS period enthalten.

## Abgrenzung: service_profile

- service_profile ist **KEIN** Teil von FastCycleContext.
- service_profile ist **Core-0-Service-Control-State** und gehört zu den
  per-Group Service-Tasks (uart_service_task, udp_service_task, etc.).
- FastCycleContext wird ausschließlich auf Core 1 (task_fast) verwendet
  und enthält nur Daten, die für den deterministischen Fast-Zyklus
  relevant sind (cycle_id, timestamp, period, durations).
- Work/Config-Moduswechsel wirken sich über service_profile auf die
  Core-0-Service-Tasks aus, nicht auf task_fast.
