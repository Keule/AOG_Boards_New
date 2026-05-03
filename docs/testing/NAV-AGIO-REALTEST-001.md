# NAV-AGIO-REALTEST-001: AgIO-End-to-End-Realtest

## 1. Ziel des Tests

End-to-End-Verifikation des PGN214-Sendepfads:
```
GNSS Snapshot / Synthetic → NAV AOG App → PGN214 Encode → TX Ring Buffer → UDP Socket → AgIO / AgOpenGPS
```

## 2. Voraussetzungen

- ESP32 mit funktionierendem Ethernet (RTL8201 RMII, DHCP)
- AgOpenGPS + AgIO auf PC im selben Subnetz
- ESP32 Firmware `nav_esp32_t_eth_lite` geflasht

## 3. Firmware-Modi

| Modus | Compile Flag | Sende-Rate | Datenquelle |
|-------|-------------|-----------|-------------|
| `REAL_GNSS` | *(default)* | 100 Hz | Live GNSS1 Snapshot |
| `SYNTHETIC` | `-D AOG_SYNTHETIC_MODE=1` | 10 Hz | Berlin Testpunkt (52.516274, 13.377704) |

### Modus aktivieren

In `platformio.ini`, Zeile entkommentieren:
```ini
    -D AOG_SYNTHETIC_MODE=1
```

Dann `pio run -e nav_esp32_t_eth_lite` neu kompilieren und flashen.

## 4. UDP/IP/Port-Konfiguration

| Parameter | Wert |
|-----------|------|
| Ziel-IP | 255.255.255.255 (Broadcast) |
| Ziel-Port | 9999 |
| Lokaler Port | 9999 |
| Protokoll | UDP, non-blocking |
| Socket-Typ | connected (setsockopt SO_BROADCAST) |
| TX-Puffer | 512 Bytes Ring Buffer (~8 PGN214 Frames) |

Die Ziel-IP kann in `app_core.c` geändert werden:
```c
#define AOG_UDP_REMOTE_PORT     9999
// remote_ip = 0xFFFFFFFF → Broadcast
// Für Unicast: z.B. 0xC0A8013C → 192.168.1.60
```

## 5. AgIO-Konfiguration

1. AgOpenGPS starten
2. AgIO: `Settings → Network → UDP Port = 9999`
3. Module Discovery: AgIO sendet PGN 253 Hello → ESP32 antwortet mit PGN 254 (IP, Port)
4. Scan: AgIO sendet PGN 202 → ESP32 antwortet mit PGN 203 (module_type=0x78/GPS, PGNs: 214, 254)

## 6. PGN214 Frame-Format (AOG v5, ADR-0006)

```
[0x80][0x81][SRC][PGN_lo][PGN_hi][LEN][DATA_0..DATA_50][CRC]
```

| Offset | Size | Field | Inhalt |
|--------|------|-------|--------|
| 0-1 | 2 | Preamble | 0x80, 0x81 |
| 2 | 1 | Source | 0x05 (GPS) |
| 3-4 | 2 | PGN | 0xD6 0x00 (214 LE) |
| 5 | 1 | Length | 0x33 (51) |
| 6-13 | 8 | Longitude | f64 LE, decimal degrees |
| 14-21 | 8 | Latitude | f64 LE, decimal degrees |
| 22-25 | 4 | Heading Dual | f32 LE, degrees (0-360) |
| 26-29 | 4 | Heading True | f32 LE, degrees (CoG) |
| 30-33 | 4 | Speed | f32 LE, km/h |
| 34-37 | 4 | Roll | f32 LE, degrees (sentinel=FLT_MAX) |
| 38-41 | 4 | Altitude | f32 LE, meters |
| 42-43 | 2 | Satellites | u16 LE |
| 44 | 1 | Fix Quality | 0=none, 1=GPS, 2=DGPS, 4=RTK_FIX, 5=RTK_FLOAT |
| 45-46 | 2 | HDOP | u16 LE, ×0.01 |
| 47-48 | 2 | Age | u16 LE, ×0.01 seconds |
| 49-50 | 2 | IMU Heading | u16 LE, ×0.1 (sentinel=0xFFFF) |
| 51-52 | 2 | IMU Roll | i16 LE, ×0.1 (sentinel=0x7FFF) |
| 53-54 | 2 | IMU Pitch | i16 LE (sentinel=0x7FFF) |
| 55-56 | 2 | IMU Yaw Rate | i16 LE (sentinel=0x7FFF) |
| 57 | 1 | CRC | sum(bytes[2..56]) mod 256 |

**Totale Frame-Größe: 59 Bytes**

## 7. Remote-Diagnosefelder

### /status (JSON)
```json
{
  "aog_output_mode": "REAL_GNSS",
  "pgn214_enabled": true,
  "pgn214_rate_hz": 100,
  "pgn214_send_count": 12345,
  "pgn214_drop_count": 5,
  "pgn214_drop_reason": "NONE",
  "pgn214_last_send_age_ms": 8,
  "pgn214_source": "GNSS1_primary",
  "pgn214_last_lat": 52.5162740,
  "pgn214_last_lon": 13.3777040,
  "pgn214_last_fix": 4,
  "pgn214_last_rtk": 4,
  "output_state": "NONE",
  "udp_bound": true,
  "udp_has_ip": true,
  "aog_udp_target_ip": "255.255.255.255",
  "aog_udp_target_port": 9999,
  "aog_udp_local_port": 9999,
  "aog_udp_tx_packets": 12345,
  "aog_udp_tx_errors": 0,
  "aog_udp_rx_packets": 12
}
```

### /diag (Plain Text)
```
AOG: mode=REAL_GNSS pgn214_sent=12345 drops=5 drop_reason=NONE rate=100Hz send_age=8ms state=NONE
  pos_valid=yes hdg_valid=yes cycles=12345 source=GNSS1_primary
  last_lat=52.5162740 last_lon=13.3777040 fix=4 rtk=4
UDP: bound=yes has_ip=yes target=255.255.255.255:9999 local=9999 tx_pkts=12345 tx_err=0 rx_pkts=12
```

### Drop-Reason-Werte
| Wert | Bedeutung |
|------|-----------|
| NONE | Alles OK |
| GNSS_NOT_VALID | Kein GNSS-Fix |
| GNSS_NOT_FRESH | GNSS-Daten abgelaufen (> 2s Timeout) |
| HEADING_INVALID | Heading-Quelle reports invalid |
| HEADING_STALE | Heading abgelaufen (> 3s Timeout) |
| HEADING_LOST | Heading-Quelle verschwunden |
| INIT | Noch kein erster Fast-Tick |
| SUPPRESSED | Output komplett unterdrückt |

## 8. Testablauf Synthetisch

1. `-D AOG_SYNTHETIC_MODE=1` in platformio.ini entkommentieren
2. `pio run -e nav_esp32_t_eth_lite` kompilieren
3. Firmware flashen
4. ESP32 Strom an → Ethernet verbinden → DHCP → IP erhalten
5. `curl http://<ESP32-IP>/status` → `aog_output_mode: "SYNTHETIC"`
6. `curl http://<ESP32-IP>/diag` → `pgn214_sent` steigt
7. AgIO auf PC starten → UDP Port 9999
8. Prüfen: AgIO zeigt Position bei Berlin (52.516, 13.378)
9. Heading rotiert langsam (+0.5°/Tick) → auf AgIO-Karte sichtbar

### PASS-Kriterien
```
aog_output_mode=SYNTHETIC ✓
pgn214_send_count steigt ✓
pgn214_rate_hz=10 ✓
AgIO empfängt PGN214 ✓
AgIO zeigt Berliner Position ✓
udp_tx_errors=0 ✓
```

## 9. Testablauf Real GNSS

1. `-D AOG_SYNTHETIC_MODE=1` in platformio.ini auskommentieren
2. Neu kompilieren und flashen
3. ESP32 mit UM980 GNSS-Empfänger verbinden
4. Outdoor: GNSS bekommt Fix (RTK oder DGPS)
5. `curl http://<ESP32-IP>/status` → `aog_output_mode: "REAL_GNSS"`
6. `pgn214_send_count` steigt mit 100 Hz
7. `pgn214_last_lat/lon` zeigt reale Position
8. `pgn214_last_fix=4` (RTK FIX) oder `2` (DGPS)
9. AgIO zeigt reale Position auf Karte

### PASS-Kriterien
```
aog_output_mode=REAL_GNSS ✓
pgn214_send_count steigt (100 Hz) ✓
snapshot_valid=true, snapshot_fresh=true ✓
AgIO zeigt reale Position ✓
Position plausibel (Outdoor-Standort) ✓
```

## 10. Testablauf Drop-Reason

1. GNSS-Empfänger abziehen oder abdecken
2. Warten bis GNSS stale wird (> 2s)
3. `curl http://<ESP32-IP>/diag` prüfen:
   - `drop_reason=GNSS_NOT_VALID` oder `GNSS_NOT_FRESH`
   - `pgn214_drop_count` steigt
   - `pos_valid=no`

### PASS-Kriterien
```
GNSS ungültig → drop_reason konkret ✓
pgn214_drop_count steigt ✓
Keine falschen Positionsdaten gesendet ✓
```

## 11. UDP-PC-Test (ohne AgIO)

### Python
```python
import socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('0.0.0.0', 9999))
print("Listening on UDP :9999...")
while True:
    data, addr = sock.recvfrom(256)
    print(f"RX from {addr[0]}:{addr[1]} len={len(data)}")
    if len(data) >= 6 and data[0] == 0x80 and data[1] == 0x81:
        pgn = data[3] | (data[4] << 8)
        print(f"  AOG frame: src=0x{data[2]:02X} PGN={pgn} len={data[5]}")
```

### Wireshark
- Filter: `udp.port == 9999`
- Erwartete Paketlänge: 59 Bytes (PGN 214 Frame)
- Hex: `80 81 05 D6 00 33 ...` (Preamble, GPS-Source, PGN 214 LE, Len=51)
- CRC prüfen: sum(bytes[2:58]) mod 256 == byte[58]

## 12. Bekannte Einschränkungen

- Heading: Wenn kein Dual-Antenne-System → heading_dual = FLT_MAX (Sentinel)
- IMU: Kein IMU angeschlossen → alle IMU-Felder = Sentinel
- UDP-Ziel: Aktuell Broadcast 255.255.255.255 — für Unicast muss Code geändert werden
- Native Tests: Vorbestehende Fehler (gnss_um980.c → esp_timer.h nicht in native verfügbar)
- Hardware-/AgIO-Test: **NICHT** in dieser Umgebung durchgeführt (keine Hardware)

## 13. Nächste Schritte

1. Hardware-Test mit synthetischem Modus (verify AgIO Akzeptanz)
2. Hardware-Test mit echtem GNSS (Outdoor)
3. UDP-Unicast-Konfiguration (einzelne AgIO-IP)
4. Optionale HTTP-API zum Umschalten REAL_GNSS ↔ SYNTHETIC (ohne Neuflashen)
5. NMEA-GSA/GSV-Diagnose erweitern (R2 Felder in remote_diag)
