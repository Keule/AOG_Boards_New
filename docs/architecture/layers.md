# Schichtenmodell

- Das System MUSS folgende Schichten verwenden:
  - HAL
  - Transport
  - Protokoll
  - App
  - Hardwaremodule
- Transport DARF KEINE Fachlogik enthalten.
- Protokoll DARF KEINEN Hardwarezugriff enthalten.
- HAL DARF KEINE Fachlogik enthalten.
