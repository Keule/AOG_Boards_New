# Steering Architecture — STEER-MIG-001

> **Task:** STEER-MIG-001 · **Version:** 1.0
> **Datum:** 2025-05-01
> **Abhaengig von:** runtime_component, safety_failsafe, steering_safety, steering_output

---

## 1. Overview

Der Steering-Pfad implementiert die Lenkregelung fuer AgOpenGPS. Er besteht aus
klar getrennten Komponenten, die ueber Snapshots und HAL-Abstraktionen
kommunizieren.

### Komponentenrollen

| Rolle | Komponente | Dateien | Aufgabe |
|-------|-----------|---------|---------|
| steering_command | `aog_steering_app` | `aog_steering_app.c/h` | PGN 252/253 parsen, SteeringInput-Snapshot produzieren |
| steering_sensor | `was_sensor` | `was_sensor.c/h` | ADC→Winkel, Freshness, Plausibilitaet |
| steering_safety | `steering_safety` | `steering_safety.c/h` | 10 Pflicht-Safety-Bedingungen pruefen |
| steering_control | `steering_control` | `steering_control.c/h` | PID-Regler, Fast-Path-Orchestrierung |
| steering_output | `steering_output` | `steering_output.c/h` | HAL-abstrahierte Motor/PWM-Ausgabe |
| steering_diagnostics | `steering_diagnostics` | `steering_diagnostics.c/h` | Fehlerhistorie, Health-Status |

---

## 2. Datenfluss

```
  AOG UDP RX
    → aog_steering_app (Core 0: service_step)
      → SteeringInput Snapshot (aog_steer_input_t)
        → steering_control (Core 1: fast_input)
          → steering_safety_evaluate() (fast_process)
            → steering_output_update() (fast_output)
              → Motor via HAL

  ADS1118 ADC
    → was_sensor (Core 0: service_step oder Core 1: fast_input)
      → WAS Snapshot (was_sensor_data_t)
        → steering_control (fast_input)
```

---

## 3. 100-Hz Fast-Pfad (Core 1)

Der Steering-Fast-Pfad arbeitet dreiphasig auf Core 1:

### fast_input
- SteeringInput-Snapshot lesen (setpoint, speed, status)
- WAS-Sensor-Snapshot lesen (actual angle, valid, fresh)
- Timestamps/Freshness vorbereiten

### fast_process
- Command-Freshness bewerten
- Sensor-Freshness bewerten
- Safety Gate auswerten (10 Bedingungen)
- Soll/Ist vergleichen → Fehler berechnen
- PID-Regler: P + I (anti-windup) + D
- Stellgroesse begrenzen (-1.0..+1.0)
- NaN/Inf Guard

### fast_output
- Wenn Safety OK: Motor-Ausgabe via HAL schreiben
- Wenn Safety NICHT OK: Motor-Aus (PWM=0, disabled)
- Diagnose-Snapshot aktualisieren

---

## 4. Safety-Gating (10 Pflichtfaelle)

| # | Bedingung | Aktion | Reason Code |
|---|----------|--------|-------------|
| 1 | Steering global disabled | Motor aus | `GLOBAL_DISABLED` |
| 2 | Lokaler Schalter AUS | Motor aus | `LOCAL_SWITCH_OFF` |
| 3 | AOG-Steer-Command stale | Motor aus | `COMMAND_STALE` |
| 4 | Kein gueltiger Command | Motor aus | `COMMAND_INVALID` |
| 5 | WAS-Sensor stale | Motor aus | `SENSOR_STALE` |
| 6 | WAS-Sensor ungueltig | Motor aus | `SENSOR_INVALID` |
| 7 | WAS-Sensor unplausibel | Motor aus | `SENSOR_UNPLAUSIBLE` |
| 8 | Zielwinkel ausserhalb Bereich | Begrenzen (nicht Motor aus) | `SETPOINT_OOR` (clamped) |
| 9 | Kommunikationsverlust | Motor aus | `COMMS_LOST` |
| 10 | Interner Fehlerstatus | Motor aus | `INTERNAL_FAULT` |

### Vorgabewerte

| Parameter | Wert | Begründung |
|-----------|------|-----------|
| Lenkwinkelbereich | -22.5° bis +22.5° | STEER-MIG-001 Spec |
| Absoluter Max | ±40° | Physischer Anschlag |
| Command Timeout | 200 ms | Konservativ, 200ms Erfahrungswert |
| Sensor Timeout | 200 ms | Gleich wie Command |
| Comms Timeout | 500 ms | Etwas grosszuegiger fuer UDP |
| PWM Bereich | -1.0 bis +1.0 | Normalisiert |
| PWM Deadzone | 5% | Vermeidet Rattern um Null |
| Default-Ausgabe | PWM=0, Motor disabled | Fail-Safe |

### Entscheidung: Setpoint OOR → Clamp (nicht Motor aus)

Setpoint ausserhalb des Lenkwinkelbereichs wird auf den Grenzwert begrenzt,
aber der Motor wird NICHT abgeschaltet. Begruendung:
- Der Operator korrigiert den Sollwinkel kontinuierlich.
- Ein Abschalten bei jedem leichten Ueberschreiten wuerde zu ruckeligem Verhalten fuehren.
- Die Begrenzung auf ±22.5° stellt sicher, dass der Motor nie ausserhalb
  des erlaubten Bereichs faehrt.

---

## 5. WAS-Sensor Skalierung

### Signalpfad
```
raw ADC (0..65535)
  → Spannung: voltage = (raw / 65535) * VREF
  → Normalisiert: norm = (raw - cal_min_raw) / (cal_max_raw - cal_min_raw)
  → Winkel: degrees = cal_min_deg + norm * (cal_max_deg - cal_min_deg)
```

### Kalibrierung (Two-Point)
| ADC Raw | Winkel |
|---------|--------|
| cal_min_raw (default: 0) | cal_min_deg (default: -40°) |
| cal_max_raw (default: 65535) | cal_max_deg (default: +40°) |

### Plausibilitaetspruefung
- ADC ausserhalb 0..65535 → RAW_OUT_OF_RANGE
- Winkel ausserhalb ±40° → ANGLE_UNPLAUSIBLE
- Sprung > max_jump_rate (default: 5°/10ms) → ANGLE_UNPLAUSIBLE
- Kalibrierspanne = 0 → CAL_SPAN_ZERO

### Sensorstatus
```
was_sensor_data_t:
  raw (uint16_t)
  voltage (float)
  degrees (float)
  valid (bool)
  fresh (bool)
  timestamp_us (uint64_t)
  reason (was_reason_t)
```

---

## 6. Motor/PWM-Abstraktion

### HAL-Interface
```c
typedef struct {
    void (*set_enable)(bool enabled);
    void (*set_direction)(bool right);
    void (*set_pwm)(float duty);
    void (*emergency_stop)(void);
} steering_output_hal_t;
```

### Verhalten
- Safety OK: PWM = |command|, Direction = sign(command), Enable = true
- Safety NOT OK: PWM = 0, Enable = false (Motor aus)
- Deadzone: |command| < 5% → PWM = 0
- Saturation: PWM auf [0, 1] begrenzt
- NaN/Inf → PWM = 0 (Guard)

### Testbarkeit
- `steering_output_mock_hal_init()` erstellt Mock-HAL
- Alle Zustandsuebergaenge pruefbar ohne GPIO

---

## 7. PID-Regler

### Standard-Konfiguration
```
Kp = 0.8    (Proportional)
Ki = 0.01   (Integral)
Kd = 0.0    (Derivative, deaktiviert)
Integral-Max = 10.0
Output-Min = -1.0
Output-Max = +1.0
dt = 10ms (100 Hz)
```

### Anti-Windup
Integral auf ±integral_max begrenzt. Verhindert Aufschaukeln bei langen
Fehlerperioden.

### PID-Reset
Bei jedem Safety-Block wird der PID-Zustand zurueckgesetzt (integral=0,
prev_error=0, initialized=false). Stellt sicher, dass nach einem
Safety-Event kein parasitaerer Integral-Betrag bestehen bleibt.

---

## 8. Tests

### Testuebersicht (62 Tests)

**steering_safety (19 Tests):**
1-19: Alle 10 Safety-Bedingungen, Default, Fault-Clear, Timeout-Boundary,
      Setpoint-Clamp, Statistiken, NULL-Handling

**steering_output (12 Tests):**
1-12: Default, Safety-Block, Richtung, Deadzone, Saturation, NaN/Inf-Guard,
      Force-Off, Mock-HAL, Deadzone-Boundary

**steering_sensor (10 Tests):**
1-10: Calibration, Span-Zero, Voltage, Plausibilitaet, Freshness, Reason-Strings,
      Lineare Interpolation

**steering_control (12 Tests):**
1-12: Default, Enable, Safety-Block, PID pos/neg, Anti-Windup, Saturation,
      NaN-Guard, Diagnostics, Service-Step-Fallback, Config

### Testablauf
```bash
# Nicht ausfuehrbar in Sandbox (kein PlatformIO / gcc native)
# Code Review: alle Teststrukturen und Assertions verifiziert
```

---

## 9. Bekannte Einschraenkungen

| # | Einschraenkung | Auswirkung | Mitigation |
|---|---------------|-----------|-----------|
| 1 | Kein IMU in Steering-Pfad | Kein Heading-Compensation | TODO: FULL-INTEGRATION-001 |
| 2 | Kein echter HAL GPIO/PWM | Skeleton-Output | Mock-HAL fuer Tests |
| 3 | PID-Parameter hardcoded | Kein Runtime-Tuning | TODO: NVS oder Config-Modus |
| 4 | Kein Autodetect fuer WAS-Kalibrierung | Manuelle Kalibrierung noetig | TODO: Kalibrierungsmodus |
| 5 | Kein PlatformIO in Sandbox | Tests nicht ausfuehrbar | Code Review |

---

## 10. Offene Punkte fuer FULL-INTEGRATION-001

- [ ] IMU-Integration in Steering-Pfad (Heading-Compensation)
- [ ] Echte HAL GPIO/PWM Implementierung
- [ ] NVS-Speicher fuer PID-Parameter und Kalibrierung
- [ ] Autodetect-Kalibrierung fuer WAS-Sensor
- [ ] Gemeinsame Runtime mit NAV-Komponenten
- [ ] Integration mit safety_failsafe (GPIO-Pin)
- [ ] Scan-Reply an AOG mit Steering-PGNs
- [ ] Runtime-Watchdog-Integration
