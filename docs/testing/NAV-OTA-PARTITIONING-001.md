# NAV-OTA-PARTITIONING-001 — ESP-IDF OTA über Ethernet

## Herkunft der Partitionierung

Die Partitionierung basiert auf dem alten Repository `Keule/ESP32_AGO_GNSS`:

- `partitions_ota.csv` — 16 MB Flash, factory + ota_0 + ota_1 + spiffs
- `partitions_ota_spiffs_nvs_dump_factory.csv` — 16 MB Flash, + coredump `dump`

Das alte Repo war **Arduino-basiert** und nutzte **SD-card OTA** via `Arduino Update` API.
Dies wurde **nicht übernommen**. Stattdessen wird **ESP-IDF-native OTA** implementiert.

## Warum SD-/Arduino-OTA nicht übernommen wurde

| Grund | Details |
|-------|---------|
| Framework-Wechsel | Neues Repo ist ESP-IDF-only, kein Arduino |
| Kein SD-Slot | LilyGO T-ETH-Lite hat keinen SD-Kartenleser |
| Transportschicht | Ethernet OTA ist sicherer und bequemer als SD-Karte |
| ESP-IDF OTA APIs | `esp_ota_ops.h` bietet sichere, getestete OTA-Funktion |

## ESP-IDF-native OTA-Konzept

```
Build (pio run) → firmware.bin
     ↓
curl POST /ota → HTTP-Server (Core 0)
     ↓
esp_ota_begin() → OTA-Partition bestimmen
httpd_req_recv() → Chunked body lesen
esp_ota_write()  → In OTA-Partition schreiben
esp_ota_end()    → Validate + Mark boot partition
     ↓
Reboot (2s Delay)
     ↓
ESP-IDF Bootloader → Neue Partition booten
```

## Partitionstabelle

**Datei:** `partitions/nav_esp32_ota_16mb.csv`

```
# Name,    Type,   SubType,  Offset,   Size,      Flags
nvs,       data,   nvs,      0x9000,   0x6000,
otadata,   data,   ota,      0xD000,   0x2000,
phy_init,  data,   phy,      0xF000,   0x1000,
factory,   app,    factory,  0x10000,  0x300000,    (3 MB — Erstinstallation per USB)
ota_0,     app,    ota_0,    0x310000, 0x300000,    (3 MB — OTA Slot A)
ota_1,     app,    ota_1,    0x610000, 0x300000,    (3 MB — OTA Slot B)
spiffs,    data,   spiffs,   0x910000, 0x6E8000,    (~7 MB — Config-Speicher)
dump,      data,   coredump, 0xFF8000, 0x8000,      (32 KB — Crash-Dump)
```

**Gesamt:** 16 MB (0x1000000)

## PlatformIO-Einstellungen

```ini
[env:nav_esp32_t_eth_lite]
board = esp32dev
board_build.flash_size = 16MB
board_upload.flash_size = 16MB
board_build.partitions = partitions/nav_esp32_ota_16mb.csv
```

## HTTP-Endpunkte

| Endpoint | Methode | Beschreibung |
|----------|---------|-------------|
| `/status` | GET | JSON-Status inkl. OTA-Zustand |
| `/diag` | GET | Text-Diagnose inkl. OTA-Zustand |
| `/version` | GET | Firmware-Version, Partition, IDF-Version |
| `/ota/status` | GET | OTA-Fähigkeit, Partitions, letztes Ergebnis |
| `/ota` | POST | Firmware-Binary hochladen (roh binär) |
| `/reboot` | POST | Kontrollierter Neustart |

## Build-Befehl

```bash
pio run -e nav_esp32_t_eth_lite
```

Output: `.pio/build/nav_esp32_t_eth_lite/firmware.bin`

## Flash-Befehl (Erstinstallation per USB)

```bash
pio run -t upload -e nav_esp32_t_eth_lite
```

## OTA-Befehl per curl

```bash
# Status prüfen
curl http://192.168.1.59/ota/status

# Version / Partition prüfen
curl http://192.168.1.59/version

# Firmware hochladen (automatischer Reboot nach 2s)
curl -X POST --data-binary @.pio/build/nav_esp32_t_eth_lite/firmware.bin http://192.168.1.59/ota

# Statusabfrage nach Reboot
curl http://192.168.1.59/status

# Manueller Reboot (falls gewünscht)
curl -X POST http://192.168.1.59/reboot
```

## Reboot-Verhalten

1. `POST /ota` empfängt das Binary
2. `esp_ota_end()` validiert das Image und setzt die Boot-Partition
3. `reboot_pending = true` wird gesetzt
4. HTTP-Antwort wird gesendet
5. **2 Sekunden Delay** → `esp_restart()`
6. ESP-IDF Bootloader startet von der neuen Partition

## Rollback / Factory-Verhalten

- ESP-IDF Bootloader unterstützt automatisch Rollback bei invalidem Image
- Bei Boot-Fehler bootet der Bootloader zurück zur vorherigen Partition
- Factory-Partition ist über USB-Flash wiederherstellbar
- `esp_ota_rollback()` ist verfügbar aber in diesem Task nicht implementiert

## OTA-Felder in /status

```json
{
  "ota_supported": true,
  "ota_ready": true,
  "ota_active": false,
  "ota_running_partition": "ota_0",
  "ota_next_partition": "ota_1",
  "ota_last_result": "ok",
  "ota_last_error": 0,
  "ota_bytes_received": 524288,
  "ota_image_size": 524288,
  "reboot_pending": false
}
```

## Bekannte Einschränkungen

1. **Keine Authentifizierung** — OTA-Endpunkt ist ungeschützt (nur lokales Netz)
2. **Kein TLS** — HTTP nur, kein HTTPS (ESP32-Ressourcen)
3. **Kein Multipart** — Binary muss als roher Body gesendet werden
4. **Kein Rollback-Button** — Nur automatischer Bootloader-Rollback
5. **Kein Progress** — Kein Upload-Fortschritt während des Transfers
6. **Hardwaretest** — Noch nicht auf echter Hardware getestet (PARTIAL ACCEPT)
7. **OTA im HTTP-Task** — Der Upload blockiert den HTTP-Thread. Parallel-Requests während Upload sind nicht möglich. Das ist akzeptabel, da OTA selten und absichtlich ausgelöst wird.

## Komponenten

| Komponente | Datei | Beschreibung |
|-----------|-------|-------------|
| `hal_ota` | `hal_ota.h / hal_ota.c` | HAL-Abstraction mit ESP-IDF `esp_ota_ops.h` Implementierung |
| `remote_diag` | `remote_diag.c` | HTTP-Server, erweitert mit `/ota`, `/ota/status`, `/reboot` |
| `partitions/` | `nav_esp32_ota_16mb.csv` | 16 MB OTA-Partitionstabelle |
| `platformio.ini` | — | Flash-Size + Partitions-Konfiguration |
