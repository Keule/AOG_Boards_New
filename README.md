# Projektziel

- Das Projekt MUSS ein ESP-IDF PlatformIO Skeleton für produktive Boards bereitstellen.
- Das Projekt DARF KEINE Arduino-Komponenten enthalten.

# Systemübersicht

- Das System MUSS ausschließlich ESP-IDF verwenden.
- Das Buildsystem MUSS PlatformIO sein.
- Das System MUSS vollständig über Ethernet bedienbar sein.
- Das System DARF NICHT von serieller Verbindung abhängig sein.

# Rollenbeschreibung

- Das System MUSS die Rolle Navigation (ESP32) unterstützen.
- Das System MUSS die Rolle Steering (ESP32-S3) unterstützen.
- Das System MUSS ein Full-Test-Profil unterstützen.

# Dokumentation

- Die Architektur-Dokumentation MUSS unter `docs/` liegen.
- Die Architektur-Dokumentation MUSS unter `docs/architecture/` liegen.
- Die ADR-Dokumentation MUSS unter `docs/adr/` liegen.
