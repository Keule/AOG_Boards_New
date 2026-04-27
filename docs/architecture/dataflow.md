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
- FastCycleContext MUSS service_profile enthalten.
