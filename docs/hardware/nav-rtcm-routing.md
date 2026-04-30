# NAV-RTCM-001: RTCM-Routing (NTRIP → Router → UM980 UART)

## Übersicht

Dieser Task aktiviert die produktive RTCM-Datenkette, die RTCM-Korrekturdaten
vom NTRIP-Client über den RTCM-Router zu beiden UM980-GNSS-Empfängern leitet.

## Datenfluss

```text
ntrip_client.rtcm_buffer
  ↓ (rtcm_router_service_step liest von Source)
rtcm_router
  ↓ (schreibt in beide Output-Buffer)
  ├→ primary_uart.tx_buffer → UART Primary TX (UM980 #1)
  └→ secondary_uart.tx_buffer → UART Secondary TX (UM980 #2)
```

### Verdrahtung (app_core.c)

```c
rtcm_router_init(&s_rtcm_router);
rtcm_router_set_source(&s_rtcm_router, &s_ntrip.rtcm_buffer);
rtcm_router_add_output(&s_rtcm_router, &s_primary_uart.tx_buffer);
rtcm_router_add_output(&s_rtcm_router, &s_secondary_uart.tx_buffer);
```

### Service-Kette

Die Runtime ruft die Service-Steps in folgender Reihenfolge auf
(je nach Service-Group-Zuordnung):

1. `ntrip_client_service_step()` — liest RTCM von TCP, schreibt in `rtcm_buffer`
2. `rtcm_router_service_step()` — liest von `rtcm_buffer`, verteilt an TX-Buffer
3. `transport_uart_service_step()` — sendet TX-Buffer über physische UART

Keine zentrale `app_core`-Monolithik — jede Component hat eigenen `service_step`.

## Architektur-Regeln

| Regel | Status |
|-------|--------|
| `rtcm_router` bleibt generisch | ✔ Kein NTRIP/UART/HAL-Abhängigkeit |
| `rtcm_router` schreibt nicht physisch UART | ✔ Nur Ring-Buffer-Schreibzugriffe |
| `ntrip_client` routet nicht selbst | ✔ Router übernimmt Verteilung |
| `transport_uart` interpretiert kein RTCM | ✔ Reiner Byte-Transport |
| Keine AOG-PGN-Änderung | ✔ |
| Keine Steering-Änderung | ✔ |

## Stats / Diagnose

### RTCM Router Stats (`rtcm_stats_t`)

| Feld | Typ | Beschreibung |
|------|-----|-------------|
| `bytes_in` | `uint32_t` | Vom Source gelesene Bytes |
| `bytes_out` | `uint32_t` | An Outputs geschriebene Bytes (Summe aller) |
| `bytes_dropped` | `uint32_t` | Bytes die nicht in Output passten |
| `last_activity_us` | `uint64_t` | Letzte Aktivität (µs) |
| `output_overflow_count` | `uint32_t` | Anzahl Output-Overflow-Events |
| `bytes_forwarded` | `uint32_t` | Per-Output: weitergeleitete Bytes |
| `bytes_dropped` | `uint32_t` | Per-Output: verlorene Bytes |

API: `rtcm_router_get_stats()`, `rtcm_router_get_output_overflow_count()`

### Transport UART TX-Stats (`transport_uart_stats_t`)

| Feld | Typ | Beschreibung |
|------|-----|-------------|
| `tx_bytes_out` | `uint32_t` | Gesendete Bytes |
| `tx_errors` | `uint32_t` | HAL Schreibfehler |
| `tx_overflows` | `uint32_t` | TX-Buffer-Overflow-Events |
| `tx_partial_writes` | `uint32_t` | Teilweise HAL-Schreibvorgänge |

API: `transport_uart_get_stats()`, `transport_uart_diagnostics()`

## Testablauf

### Host-/Sim-Tests (gcc native)

| Test | Datei | Ergebnis |
|------|-------|---------|
| RTCM-Routing zu beiden Outputs | `test_rtcm_router.c` (21 Tests) | ✔ |
| Identische Bytes auf beiden UART-TX | `test_both_outputs_identical_bytes` | ✔ |
| Primary voll → Secondary unbeeinträchtigt | `test_full_primary_does_not_affect_secondary` | ✔ |
| Output-Overflow-Counting | `test_output_overflow_count_increments` | ✔ |
| TX-Errors / TX-Overflows | `test_transport_uart.c` (25 Tests) | ✔ |
| End-to-End Kette | `test_nav_chain.c` (5 Tests) | ✔ |
| Router ohne HAL-Abhängigkeit | `test_router_is_generic_no_hal_dependency` | ✔ |

### Hardware-Smoke-Test (TBD)

1. NTRIP Fake-Caster liefert RTCM-Daten
2. Beide UART TX-Zähler steigen (`tx_bytes_out`)
3. `output_overflow_count` bleibt 0 bei ausreichend Buffer
4. Keine Crashes

## Fehlerfälle

### TX-Buffer voll (Output Overflow)

Wenn ein UART-TX-Buffer voll ist, werden überschüssige Bytes gedroppt:
- `rtcm_router` inkrementiert `bytes_dropped` und `output_overflow_count`
- Der andere Output ist **nicht** betroffen (unabhängige Buffer)

### HAL-Schreibfehler

Wenn `hal_uart_write()` negativ zurückgibt:
- `tx_errors` wird inkrementiert
- Bytes bleiben im TX-Buffer (kein Consume)
- Nächster Service-Step versucht erneut

### NTRIP Disconnect

Bei NTRIP-Fehler/Timeout:
- `rtcm_buffer` wird geleert
- RTCM Router liest 0 Bytes → nichts wird verteilt
- Nach Reconnect fließt RTCM automatisch wieder

## Nicht Teil dieses Tasks

- RTCM decodieren (MSP/MTK1005)
- UM980 automatisch konfigurieren (NMEA SET)
- AOG PGN 214 (Steering)
- Heading-Berechnung (Dual-GNSS)
- Autosteering
