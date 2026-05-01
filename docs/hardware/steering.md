# Steering Hardware Interface — STEER-MIG-001

> **Task:** STEER-MIG-001 · **Version:** 1.0
> **Datum:** 2025-05-01

---

## 1. Komponentenuebersicht

| Komponente | Verzeichnis | Dateien |
|-----------|------------|---------|
| steering_safety | `components/steering_safety/` | `.c/.h/CMakeLists.txt` |
| steering_output | `components/steering_output/` | `.c/.h/CMakeLists.txt` |
| steering_diagnostics | `components/steering_diagnostics/` | `.c/.h/CMakeLists.txt` |
| steering_control (erweitert) | `components/steering_control/` | `.c/.h/CMakeLists.txt` |
| was_sensor (erweitert) | `components/was_sensor/` | `.c/.h/CMakeLists.txt` |
| aog_steering_app (unveraendert) | `components/aog_steering_app/` | `.c/.h/CMakeLists.txt` |
| actuator_drv8263h (bestehend) | `components/actuator_drv8263h/` | `.c/.h/CMakeLists.txt` |
| safety_failsafe (bestehend) | `components/safety_failsafe/` | `.c/.h/CMakeLists.txt` |

## 2. Aenderungsuebersicht

### Neue Komponenten
- **steering_safety**: Reine Logik, 10 Safety-Bedingungen, keine Hardware
- **steering_output**: HAL-abstrahierte Motorausgabe, mockbar
- **steering_diagnostics**: Fehlerhistorie, Health-Status

### Erweiterte Komponenten
- **steering_control**: Fast-Path-Hooks (fast_input/process/output), PID, Safety-Integration
- **was_sensor**: Freshness-Tracking, Plausibilitaetspruefung, erweitertes Snapshot

### Unveraenderte Komponenten
- **aog_steering_app**: PGN 252/253 Parsing, SteeringInput-Snapshot
- **actuator_drv8263h**: DRV8263H Skeleton (bleibt bestehen, wird durch steering_output ergaenzt)
- **safety_failsafe**: Watchdog + GPIO-Failsafe

## 3. CMake Abhaengigkeiten

```
steering_safety   → runtime_types
steering_output   → runtime_types
steering_diagnostics → steering_control, runtime_snapshot
steering_control  → runtime_components, runtime_types, runtime_snapshot,
                    protocol_aog, steering_safety, steering_output
was_sensor        → runtime_components, runtime_types, runtime_snapshot, ads1118
```

## 4. Hardware-Details

### WAS-Sensor (Lenkwinkelsensor)
| Parameter | Wert |
|-----------|------|
| Sensor | Potentiometer |
| Messbereich | 0..3V |
| ADC | ADS1118 (SPI, 16-bit) |
| VREF | 3.3V |
| Lenkwinkelbereich | -22.5° bis +22.5° |
| Kalibrierung | 2-Punkt linear |

### Motor-Ausgabe
| Parameter | Wert |
|-----------|------|
| Treiber | DRV8263H |
| PWM | 0..100% (normalisiert -1.0..+1.0) |
| Direction | 1 GPIO (links/rechts) |
| Enable | 1 GPIO (Motor ein/aus) |
| Deadzone | 5% |
| Max PWM | 100% |

## 5. Safety-Matrix

| Bedingung | Default | Timeout | Aktion |
|-----------|---------|---------|--------|
| Global enabled | false | - | Motor aus |
| Local switch | false | - | Motor aus |
| Command stale | - | 200ms | Motor aus |
| Sensor stale | - | 200ms | Motor aus |
| Sensor unplausible | - | - | Motor aus |
| Setpoint OOR | - | - | Begrenzen auf ±22.5° |
| Comms lost | - | 500ms | Motor aus |
| Internal fault | false | - | Motor aus |
