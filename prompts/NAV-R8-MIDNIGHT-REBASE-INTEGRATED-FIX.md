# Prompt für Agent – NAV-R8-MIDNIGHT-REBASE-INTEGRATED-FIX

## Titel
NAV-R8-MIDNIGHT-REBASE-INTEGRATED-FIX: Vom Mitternachtsstand aus Boot/RemoteDiag/OTA, NTRIP/RTCM, UM980-GNSS-Konfiguration und Snapshot-Gating in einem sauberen Durchlauf reparieren

## Kontext
Wir nehmen den Stand von heute 0:00 Uhr als Basis und integrieren die im Tagesverlauf gewonnenen dauerhaften Erkenntnisse sauber neu. Nicht blind den Tagesstand übernehmen, sondern die validierten Erkenntnisse aus den Dokumenten verwenden.

Verbindliche Kontextdateien:
- `docs/adr/ADR-020-nav-boot-safe-gnss-ntrip-lessons.md`
- `docs/testing/NAV-R8-RECOVERY-TEST-MATRIX.md`

## Ziel
Ein konsistenter NAV-Stand, der:
1. sicher bootet,
2. Ethernet/RemoteDiag/OTA stabil bereitstellt,
3. NTRIP über local_secrets/compile-time config korrekt startet,
4. RTCM verlustfrei an beide GNSS-Empfänger routet,
5. UM980 RX1/RX2 receiver-spezifisch korrekt konfiguriert,
6. GNSS valid/fresh nachvollziehbar macht,
7. AOG_NAV erst bei gültigem Snapshot freigibt.

## Harte Regeln
```text
- Kein Netzwerkcode in task_fast.
- Kein blockierender GNSS-Restore im Bootpfad.
- RemoteDiag/OTA dürfen nicht von GNSS, NTRIP, RTCM oder AOG_NAV abhängen.
- UART-RX pro Receiver nur ein Besitzer.
- Keine konkurrierenden HTTP-Direct-UART-Reads.
- Keine Secrets im Klartext loggen.
- Keine stillen Pinänderungen.
- GPIO34 bleibt GNSS2 RX.
- ESP-IDF/PlatformIO only, kein Arduino.
```

## Aufgabe A – Boot/RemoteDiag/OTA als geschützte Basis herstellen

Akzeptanz:
```text
BOARD_PROFILE: profile=lilygo_t_eth_lite_esp32 role=NAV
BOARD_PROFILE: BOARD PIN SANITY CHECK: PASS
HAL_ETH: ETH: got_ip=...
REMOTE_DIAG: endpoints registered: /status,/diag,/version,/logs/raw,/logs/view,/logs,/favicon.ico,/ota,/reboot
GET /ota funktioniert
POST /ota funktioniert
```

## Aufgabe B – NTRIP Config Injection und RTCM Routing übernehmen

NTRIP muss aus `local_secrets` oder compile-time defaults in die Runtime gelangen.

Akzeptanz:
```text
NTRIP_CFG: source=local_secrets host=set port=2101 mount=set user=set password=set
NTRIP: eth_ready=1 config_ready=1 state=connected rx_bytes increasing
RTCM_ROUTER: in_bytes increasing out1_bytes increasing out2_bytes increasing drops=0
GNSS_RTCM_TX primary/secondary tx_bytes increasing drops=0
```

## Aufgabe C – UM980 Update-Frequenz zentral und receiver-spezifisch konfigurieren

Die Frequenz wird über UM980-Commands gesetzt:
```text
LOG COMx GNGGA ONTIME 0.10  # 10 Hz
LOG COMx GNRMC ONTIME 0.10  # 10 Hz
LOG COMx GNGSA ONTIME 1.0   # 1-Hz Epoch, ggf. 3 Sätze/s wegen Multi-Talker
UNLOG COMx ...GSV...        # GSV aus
GST disabled unless explicitly enabled
```

Implementiere ein receiver-spezifisches Profil:
```c
typedef struct {
    int receiver_id;
    const char *candidate_ports[]; // e.g. COM1, COM2 for RX1; COM2 known for RX2
    const char *selected_port;
    float gga_hz;
    float rmc_hz;
    float gsa_epoch_hz;
    bool gst_enabled;
    bool gsv_enabled;
} gnss_nmea_profile_t;
```

RX2 darf mit COM2 starten. RX1 darf nicht fest COM2 annehmen: RX1 muss COM1/COM2 per ACK+Effect-Matrix prüfen oder konfigurierbar wählen.

Akzeptanz RX2:
```text
GNSS_CFG2 state=DONE port=COM2
gga=8..12 rmc=8..12 gsv=0 gst=0 if disabled gsa=OK_MULTI_TALKER if sentence-rate 3Hz
```

Akzeptanz RX1:
```text
GNSS_CFG rx1 state=DONE selected_port=COM1/COM2
gga=8..12 rmc=8..12 gsv=0 gst=0 if disabled
```

Falls RX1 nicht fixbar ist:
```text
GNSS_CFG rx1 state=FAILED
matrix results for COM1 and COM2 logged
reason=NO_ACK/NO_EFFECT/GSV_ACTIVE/RATE_MISMATCH
```

## Aufgabe D – Command-Erfolg nur durch ACK + Effect bewerten

Nicht akzeptabel:
```text
ACK OK aber falsche Raten als OK verkaufen
TIMEOUT aber keine Effektprüfung
```

Erforderlich:
```text
receiver | port | command | ack_status | effect_after | decision
```

## Aufgabe E – GSA/GST korrekt klassifizieren

GSA:
```text
GSA epoch=1.0Hz sentence_rate=3.0Hz talkers=3 status=OK_MULTI_TALKER
```

GST:
```text
GST_DISABLED -> gst=0 expected
GST_UNEXPECTED -> GST läuft trotz disabled
GST_MISSING -> GST enabled but absent
GST_OK -> enabled and in target range
```

## Aufgabe F – GNSS Snapshot Drop-Reasons einführen

Aktuelles Problem: NMEA-Sätze laufen, aber `GNSS valid=0 fresh=0`; `vGGA_age=4294967295` / `vRMC_age=4294967295` darf nicht mehr unkommentiert erscheinen.

Pflichtdiagnose:
```text
GNSS_VALIDITY primary:
  last_gga_seen_age=...
  last_gga_valid_age=invalid|...
  gga_fix_quality=...
  last_rmc_seen_age=...
  last_rmc_valid_age=invalid|...
  rmc_status=A|V|unknown
  position_valid=0/1
  motion_valid=0/1
  fresh=0/1
  drop_reason=...
```

Drop-Reasons mindestens:
```text
NO_GGA
GGA_FIX_QUALITY_0
GGA_PARSE_ERROR
GGA_STALE
NO_RMC
RMC_STATUS_V
RMC_STALE
SNAPSHOT_NOT_COMMITTED
```

## Aufgabe G – Tests und Nachweise

Pflicht:
```text
pio run -e nav_esp32_t_eth_lite
pio test -e native
Bootlog mindestens 180s
/status /diag /logs/view Auszüge
NTRIP connected Nachweis
RTCM bytes increasing Nachweis
RX1/RX2 GNSS_CFG Ergebnisse
GNSS_VALIDITY Drop-Reasons
```

## ACCEPT
```text
- Boot/ETH/RemoteDiag/OTA stabil.
- NTRIP connected und RTCM routed to both receivers.
- UART rx_overruns=0, tx_drops=0.
- RX2 Profil korrekt.
- RX1 entweder korrekt konfiguriert oder mit COM1/COM2-Matrix sauber als nicht steuerbar dokumentiert.
- GNSS valid/fresh Drop-Reasons sichtbar.
- Keine 4294967295-Ages ohne Erklärung.
- task_fast stabil ~100Hz.
```

## REJECT
```text
- Ethernet/RemoteDiag/OTA regressieren.
- NTRIP_CFG wieder host/mount empty.
- NTRIP connected fehlt trotz Config.
- RTCM nicht an beide GNSS geht.
- RX1 bleibt blind COM2 ohne Portmatrix.
- GSV_ACTIVE auf RX1 wird als OK/WARN verkauft.
- GNSS valid=0 ohne Drop-Reasons.
- UART-RX konkurrierend gelesen wird.
```

## Erwartete Lieferung
```text
1. Code-Diff / Commit.
2. Liste geänderter Dateien.
3. Erklärung, welche Tagesfixes übernommen wurden.
4. Buildlog.
5. Host-Testlog.
6. Bootlog mindestens 180s.
7. /status, /diag, /logs/view Auszüge.
8. RX1/RX2 COM-Port-/Effect-Matrix.
9. NTRIP/RTCM Nachweis.
10. GNSS_VALIDITY Drop-Reasons.
11. Offene Risiken.
```
