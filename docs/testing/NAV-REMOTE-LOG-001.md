# NAV-REMOTE-LOG-001: Remote Log Ringbuffer

## Ziel / Goal

Alle ESP_LOG-Ausgaben in einen statischen RAM-Ringbuffer (32KB) spiegeln und ueber HTTP-Endpoints abrufbar machen. Erlaubt Echtzeit-Debugging ohne serielle Verbindung.

## Architektur

```
ESP_LOG*() → esp_log_set_vprintf() hook → remote_log_vprintf()
                                              │
                                              ├─ vsnprintf(line, 384)
                                              ├─ xSemaphoreTryTake(mutex, 0)
                                              │   ├─ OK: ringbuffer_append(line)
                                              │   └─ FAIL: lines_dropped++
                                              └─ original_vprintf(fmt, ap)  → UART
```

### Ringbuffer

- **Groesse**: 32KB statisch (`.bss`)
- **Typ**: Circular overwrite buffer
- **Max Zeilenlaenge**: 384 Bytes (abgeschnitten)
- **Statistiken**: lines_written, lines_dropped, bytes_written, bytes_overwritten, used_bytes

### vprintf Hook Regeln

| Regel | Grund |
|-------|-------|
| `va_copy()` vor `vsnprintf()` | va_list kann nur einmal consumed werden |
| Keine `ESP_LOG*()` Aufrufe | Endlosschleife (Rekursion) |
| Kein `malloc`/`free` pro Zeile | Heap-Fragmentierung, Latenz |
| `xSemaphoreTryTake(0)` | Nicht blockieren — Zeile droppen statt warten |
| Keine Netzwerk-Operationen | Deadlock mit HTTP-Server-Stack |

## Endpunkte

### `GET /logs`
Gibt den gesamten Ringbuffer-Inhalt als `text/plain` zurueck.

```bash
curl http://192.168.1.100/logs
```

### `GET /logs?tail=N`
Gibt die letzten N Zeilen zurueck.

```bash
curl http://192.168.1.100/logs?tail=50
curl http://192.168.1.100/logs?tail=200
```

### `GET /logs/status`
JSON mit Pufferstatistik.

```bash
curl http://192.168.1.100/logs/status
```

Antwort:
```json
{
  "remote_log_enabled": true,
  "buffer_size": 32768,
  "used_bytes": 12345,
  "lines_written": 567,
  "lines_dropped": 2,
  "bytes_written": 45678,
  "bytes_overwritten": 12345
}
```

### `POST /logs/clear`
Leert den Ringbuffer und setzt alle Zaehler zurueck.

```bash
curl -X POST http://192.168.1.100/logs/clear
```

Antwort:
```json
{"ok":true}
```

### `GET /logs/view`
Auto-refreshendes HTML-Interface. Laedt alle 1 Sekunde die letzten 200 Zeilen.

```bash
# Browser: http://192.168.1.100/logs/view
curl http://192.168.1.100/logs/view
```

## HTTP-Handler Architektur

Die 4 neuen Endpunkte sind in `remote_diag.c` registriert ( gleicher HTTP-Server wie `/status`, `/diag`, `/version`).

Antwort-Puffer fuer `/logs` und `/logs?tail=N` wird per `malloc(32768)` auf dem Heap allokiert (32KB Stack wuerde den HTTP-Handler-Stack sprengen).

## Beispiel-Session

```bash
# 1. Status pruefen
curl -s http://192.168.1.100/logs/status | python3 -m json.tool

# 2. Letzte 50 Zeilen lesen
curl -s http://192.168.1.100/logs?tail=50

# 3. Buffer leeren
curl -s -X POST http://192.168.1.100/logs/clear

# 4. Browser-Viewer oeffnen
open http://192.168.1.100/logs/view
```

## Bekannte Einschraenkungen

- **Kein WebSocket**: Nur Polling (curl oder Browser-Refresh)
- **Keine Persistenz**: Buffer ist fluechtig — bei Reboot geloescht
- **Kein interaktiver CLI**: Nur HTTP-API
- **32KB Limit**: Bei hoher Log-Rate koennen alte Zeilen schnell ueberschrieben werden
- **Max 384 Bytes/Zeile**: Laengere Zeilen werden abgeschnitten
- **lines_dropped**: Koennen auftreten wenn HTTP-Handler den Mutex haelt waehrend ein Log-Eintrag geschrieben wird

## Performance-Regeln

1. **Log-Hook darf NICHT blockieren** — try-lock mit timeout=0
2. **Log-Hook darf NICHT rekursiv loggen** — keine ESP_LOG*() Aufrufe
3. **Kein Heap pro Zeile** — Buffer ist statisch
4. **HTTP-Handler nutzen blocking lock** — Only one reader at a time, aber das ist OK (HTTP-Server ist single-threaded)
5. **Keine Aenderungen an task_fast / Core 1**

## Dateien

| Datei | Aenderung |
|-------|-----------|
| `components/remote_log/include/remote_log.h` | NEU: Header mit API |
| `components/remote_log/remote_log.c` | NEU: Ringbuffer + vprintf hook |
| `components/remote_log/CMakeLists.txt` | NEU: ESP-IDF component |
| `components/remote_diag/remote_diag.c` | GEAENDERT: 4 HTTP-Handler + URI-Registrierung |
| `components/remote_diag/remote_diag.h` | GEAENDERT: MAX_URI_HANDLERS 8→12, Kommentar |
| `components/remote_diag/CMakeLists.txt` | GEAENDERT: REQUIRES += remote_log |
| `components/app_core/app_core.c` | GEAENDERT: remote_log_init() nach HAL-Init |
| `platformio.ini` | GEAENDERT: lib_ignore += remote_log (native) |
