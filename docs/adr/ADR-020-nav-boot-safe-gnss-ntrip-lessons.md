# ADR-020: NAV Boot-Safe, GNSS/UM980, NTRIP und Diagnose-Erkenntnisse vom 2026-05-05

## Status
Accepted for next NAV recovery/refactor pass.

## Kontext
Am 2026-05-05 wurden mehrere Runden Boot-, RemoteDiag-, OTA-, UART-, GNSS- und NTRIP-Fixes getestet. Der Stand um Mitternacht soll als Basis genommen werden, aber die Tageserkenntnisse müssen dauerhaft in Repo-Dokumentation und Agentenprompt einfließen.

## Dauerhafte Erkenntnisse

### Boot / Init / RemoteDiag
- Board/Profile und Pin-Sanity müssen vor Fachlogik sichtbar sein.
- Ethernet-HAL muss im NAV-Build immer initialisiert werden.
- RemoteDiag/HTTPD darf nicht von GNSS-Restore, NTRIP, RTCM oder AOG_NAV abhängen.
- `/status`, `/diag`, `/version`, `/logs/raw`, `/logs/view`, `/logs`, `/ota`, `/reboot` müssen registriert sein, wenn RemoteDiag/OTA aktiv sind.
- OTA ist Remote-Maintenance, nicht task_fast, nicht GNSS-abhängig.

### UART / GNSS
- UART-Hardware/Drain ist nach den Tagesfixes nicht mehr Hauptproblem: `rx_overruns=0`, `rx_ring` stabil, RTCM-TX zu beiden GNSS ohne Drops.
- GPIO34 ist für GNSS2 RX der bestätigte stabile Pin; GPIO35 war problematisch.
- GNSS2/RX2 ist mit UM980-Port `COM2` steuerbar.
- GNSS1/RX1 ist mit festem `COM2` nicht steuerbar: Commands laufen in TIMEOUT, NMEA läuft weiter, GSV bleibt aktiv.
- RX1 darf nicht blind über `COM2` konfiguriert werden; RX1 braucht eine COM1/COM2-Portmatrix oder receiver-spezifische Portkonfiguration.
- Die UM980 Update-Frequenzen werden über `LOG COMx <sentence> ONTIME <period>` gesetzt, z. B. `LOG COM2 GNGGA ONTIME 0.10` für 10 Hz.
- Command-Erfolg darf nicht nur ACK-basiert bewertet werden. Es braucht ACK-Auswertung plus Effekt-Verifikation der realen NMEA-Raten.
- GSA `3 Hz` ist bei 1-Hz-Epoch plausibel, wenn mehrere GSA-Sätze/Talker pro Epoch ausgegeben werden. Diagnose muss zwischen sentence_rate und epoch_rate unterscheiden.
- GST muss eindeutig klassifiziert werden: `GST_DISABLED`, `GST_OK`, `GST_UNEXPECTED` oder `GST_MISSING`.

### NTRIP / RTCM
- NTRIP-Konfiguration muss aus `local_secrets` oder compile-time defaults in Runtime-Config übernommen werden.
- Passwörter dürfen nie im Klartext geloggt werden; nur `password=set/empty/masked`.
- Erfolgreicher Zustand: `NTRIP config_ready=1 state=connected`, `RTCM_ROUTER in_bytes` steigt, `out1/out2` steigen, `GNSS_RTCM_TX` für beide Empfänger steigt, Drops bleiben 0.

### GNSS Snapshot / AOG_NAV
- Trotz stabiler NMEA-Zähler und RTCM bleiben `GNSS valid=0 fresh=0` und `AOG_NAV state=GNSS_INVALID` möglich.
- Dauerhafte `vGGA_age=4294967295` / `vRMC_age=4294967295` sind ein Sentinel-/Last-valid-Problem oder ein klarer Hinweis, dass keine gültigen GGA/RMC in den Snapshot übernommen werden.
- Snapshot-Gating braucht Drop-Reasons: `NO_GGA`, `GGA_FIX_QUALITY_0`, `GGA_STALE`, `NO_RMC`, `RMC_STATUS_V`, `RMC_STALE`, `SNAPSHOT_NOT_COMMITTED` usw.
- Heading kann zeitweise valid sein, obwohl NAV-Snapshot invalid bleibt; Diagnose muss position_valid, motion_valid, heading_valid getrennt ausgeben.

## Konsequenz
Der nächste Patch darf nicht wieder inkrementell nur Symptome reparieren. Vom Mitternachtsstand aus sollen die heutigen dauerhaften Erkenntnisse sauber integriert werden: Boot-Safe/RemoteDiag/OTA, NTRIP/RTCM, GNSS_CFG Portmatrix, NMEA-Profil, Snapshot-Drop-Reasons und robuste Diagnose.
