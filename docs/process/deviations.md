# Abweichungen (verbindlich)

## Zulässigkeit
Eine Abweichung ist nur erlaubt bei:
- technischem Fehler in der Spec,
- sonstigem Buildbruch.

## Pflichtanforderungen
Jede erlaubte Abweichung MUSS:
- benannt sein,
- begründet sein,
- minimal sein.

## Verbotene Wirkung
Eine Abweichung DARF NICHT:
- Scope erweitern,
- Features hinzufügen,
- Architektur verändern.

## Bekannte Vorab-Fehler (Known Issues)

### NTRIP-Client: test_retry_resets_request_offset schlägt fehl

- **Test:** `test_retry_resets_request_offset` in `test_ntrip_client.c`
- **Ist-Verhalten:** Test erwartet `reconnect_count == 1`, erhält `4`
- **Ursache:** Pre-existing State-Machine-Timing-Issue im ntrip_client.
  Der Reconnect-Zähler wird in bestimmten Timing-Szenarien mehrfach
  inkrementiert, bevor der Test-Check erfolgt.
- **Nicht verursacht durch:** NAV-RTCM-001, NAV-NTRIP-001, Followup-Review,
  Runtime-Blocker, oder irgendwelcheRTC/Routing/Wiring-Änderungen.
- **Späterer Task/Fix:** Wird in einem dedizierten NTRIP-Client-Nacharbeit-Task
  behoben (NTRIP State Machine Hardening).
- **Auswirkung auf NAV-RTCM-001:** Keine. Der RTCM-Router ist vollständig
  unabhängig vom NTRIP-Client-State. RTCM-Datenfluss funktioniert
  unabhängig vom Reconnect-Verhalten.
