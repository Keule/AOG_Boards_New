# NAV-NTRIP-001: NTRIP Client — Setup & Dokumentation

## Überblick

Der NTRIP-Client implementiert das NTRIP-Protokoll (Networked Transport of RTCM via Internet Protocol) auf Basis des generischen `transport_tcp` Components. Er verbindet sich mit einem NTRIP-Caster, authentifiziert sich via HTTP Basic Auth und empfängt RTCM-Korrekturdaten.

### Datenfluss

```text
┌─────────────────┐     TCP      ┌──────────────────┐    HTTP GET     ┌─────────────────┐
│  NTRIP Caster   │ ◄───────────►│  transport_tcp   │ ◄────────────── │  ntrip_client    │
│  (remote, :2101)│              │  (rx/tx 512B)    │                │  (state machine) │
└─────────────────┘              └──────────────────┘                └────────┬────────┘
                                                                            │ RTCM
                                                                            ▼
                                                                   ┌─────────────────┐
                                                                   │  rtcm_buffer    │
                                                                   │  (512B ring)    │
                                                                   └────────┬────────┘
                                                                            │
                                                                   ┌────────▼────────┐
                                                                   │  rtcm_router    │
                                                                   │  (Verteilung)   │
                                                                   └─────────────────┘
```

## NTRIP-Konfiguration

### Konfigurationsstruktur (`ntrip_client_config_t`)

| Feld | Typ | Beschreibung | Default |
|------|-----|--------------|---------|
| `mountpoint` | `const char*` | NTRIP-Mountpoint (z.B. "VRTK") | — (Pflicht) |
| `username` | `const char*` | Benutzername für Basic Auth | `NULL` (kein Auth) |
| `password` | `const char*` | Passwort für Basic Auth | `NULL` |
| `user_agent` | `const char*` | User-Agent Header | `"AOG-ESP-Multiboard/1.0"` |
| `reconnect_initial_ms` | `uint32_t` | Initiale Reconnect-Backoff (ms) | `1000` |
| `reconnect_max_ms` | `uint32_t` | Maximaler Reconnect-Backoff (ms) | `60000` |
| `response_timeout_ms` | `uint32_t` | Timeout für Connect + Response (ms) | `10000` |

### Platzhalter-Konfiguration

Die Konfiguration wird zur Laufzeit übergeben. Keine Secrets im Code.

```c
ntrip_client_config_t ntrip_config;
ntrip_client_config_set_defaults(&ntrip_config);
ntrip_config.mountpoint = "MyMountpoint";   /* Vom Caster bereitgestellt */
ntrip_config.username   = "my_user";         /* Später: NVS oder Web-UI */
ntrip_config.password   = "my_pass";         /* Später: NVS oder Web-UI */

ntrip_client_configure(&ntrip, &ntrip_config);
```

### NVS-Vorbereitung (zukünftig)

Die Konfigurationsstruktur ist so ausgelegt, dass sie direkt aus NVS befüllt werden kann:
- Alle Strings sind `const char*` — können auf NVS-backed Buffer zeigen
- Numerische Werte sind `uint32_t` — direkt aus NVS lesbar
- `ntrip_client_config_set_defaults()` setzt alle Werte auf sichere Defaults

## State Machine

```text
        ┌──────────┐
        │   IDLE   │◄──────────────────────────────────┐
        └────┬─────┘                                    │
             │ ntrip_client_start()                      │ ntrip_client_stop()
             ▼                                          │
     ┌──────────────┐                                   │
     │  CONNECTING  │                                   │
     └──────┬───────┘                                   │
            │ transport_tcp connected                   │
            ▼                                           │
     ┌──────────────┐                                   │
     │ SEND_REQUEST │                                   │
     └──────┬───────┘                                   │
            │ request written                           │
            ▼                                           │
     ┌──────────────┐     200 OK      ┌────────────┐   │
     │WAIT_RESPONSE │ ──────────────► │ STREAMING  │───┘
     └──────┬───────┘                  └─────┬──────┘
            │                                │
            │ error / timeout                │ disconnect
            ▼                                ▼
     ┌──────────────┐                 ┌──────────────┐
     │    ERROR     │ ──backoff──►    │  RETRY_WAIT  │
     └──────────────┘                 └──────┬───────┘
                                             │
                                             │ backoff expired
                                             ▼
                                      ┌──────────────┐
                                      │  CONNECTING  │
                                      └──────────────┘
```

### Zustände

| Zustand | Beschreibung |
|---------|-------------|
| `IDLE` | Nicht gestartet. Wartet auf `ntrip_client_start()`. |
| `CONNECTING` | TCP-Verbindung wird aufgebaut (via `transport_tcp`). |
| `SEND_REQUEST` | HTTP/NTRIP-Request wird in TX-Buffer geschrieben. |
| `WAIT_RESPONSE` | Wartet auf HTTP-Response-Header vom Caster. |
| `STREAMING` | Verbunden. RTCM-Daten werden weitergeleitet. |
| `ERROR` | Fehler aufgetreten (HTTP-Fehler, Timeout, Disconnect). |
| `RETRY_WAIT` | Exponentieller Backoff vor dem nächsten Verbindungsversuch. |

## HTTP/NTRIP-Request

Der Client erzeugt folgenden Request:

```http
GET /<mountpoint> HTTP/1.0
User-Agent: AOG-ESP-Multiboard/1.0
Authorization: Basic <base64(user:pass)>
Ntrip-Version: Ntrip/2.0
Connection: close

```

### Akzeptierte Responses

| Response | Status | Ergebnis |
|----------|--------|----------|
| `ICY 200 OK` | Erfolgreich | → STREAMING |
| `HTTP/1.0 200` | Erfolgreich | → STREAMING |
| `HTTP/1.1 200` | Erfolgreich | → STREAMING |
| `HTTP/1.x 401` | Auth-Fehler | → ERROR → RETRY_WAIT |
| `HTTP/1.x 403` | Verboten | → ERROR → RETRY_WAIT |
| `HTTP/1.x 404` | Mountpoint nicht gefunden | → ERROR → RETRY_WAIT |

## Test mit Caster

### Voraussetzungen

- ESP32-Board mit Ethernet (W5500 oder internes MAC)
- NTRIP-Caster erreichbar (öffentlich oder lokal)
- Ethernet-Verbindung hergestellt (`hal_eth` initialisiert)

### Manuelles Testen (Serieller Monitor)

```
I (3000) ntrip_client: state idle → connecting
I (3100) ntrip_client: TCP connecting to 192.168.1.100:2101
I (3200) ntrip_client: state connecting → send_request
I (3201) ntrip_client: state send_request → wait_response
I (3250) ntrip_client: state wait_response → streaming
I (3250) ntrip_client: ICY 200 OK, RTCM streaming active
```

### Fehlerfall-Logs

```
W (3200) ntrip_client: state wait_response → error (401)
W (4200) ntrip_client: reconnect #1, backoff=1000ms
I (5200) ntrip_client: state retry_wait → connecting
```

```
W (4200) ntrip_client: state error (timeout)
W (5200) ntrip_client: reconnect #1, backoff=1000ms
```

### Automatisierte Host-Tests

```bash
# Alle Host-Tests ausführen
pio test -e native

# Nur NTRIP-Tests
pio test -e native -f test_ntrip_client

# Nur Nav-Chain-Sim-Test
pio test -e native -f test_nav_chain
```

## Fehlerfälle

| Fehler | Ursache | Behandlung |
|--------|---------|-----------|
| `NTRIP_ERR_NO_TRANSPORT (-1)` | Kein transport_tcp gesetzt | ERROR, kein Retry |
| `NTRIP_ERR_NO_MOUNTPOINT (-3)` | Mountpoint ist NULL | ERROR, kein Retry |
| `NTRIP_ERR_TIMEOUT (-8)` | Caster antwortet nicht innerhalb `response_timeout_ms` | ERROR → exponentialer Backoff → Retry |
| `NTRIP_ERR_DISCONNECTED (-7)` | TCP-Verbindung verloren | ERROR → exponentialer Backoff → Retry |
| HTTP 401/403 | Falsche Credentials | ERROR → exponentialer Backoff → Retry |
| HTTP 404 | Mountpoint existiert nicht | ERROR → exponentialer Backoff → Retry |

## Exponentieller Backoff

- Initial: `reconnect_initial_ms` (default: 1000ms)
- Faktor: 2× pro Retry
- Maximum: `reconnect_max_ms` (default: 60000ms)
- Reset: Bei erfolgreicher Verbindung (→ STREAMING)

Beispiel: 1s → 2s → 4s → 8s → 16s → 32s → 60s → 60s → ...

## Nicht Teil dieses Tasks

- RTCM an UM980 senden
- AOG PGNs
- UM980-Konfiguration
- Heading
- Steering
- RTCM-Decodierung
