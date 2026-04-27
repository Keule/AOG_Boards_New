# Architekturüberblick

- Das System MUSS ausschließlich ESP-IDF verwenden.
- Das System DARF KEINE Arduino-Komponenten enthalten.
- Das Buildsystem MUSS PlatformIO sein.
- Das System MUSS die Rolle Navigation (ESP32) unterstützen.
- Das System MUSS die Rolle Steering (ESP32-S3) unterstützen.
- Das System MUSS ein Full-Test-Profil unterstützen.
- Ethernet MUSS immer verfügbar sein.
- Das System MUSS vollständig über Ethernet bedienbar sein.
- Das System DARF NICHT von serieller Verbindung abhängig sein.
