# Task-Typen

## Übersicht
- **RSK**: Risiko-/Regel- und Safety-bezogene Aufgaben
- **ARC**: Architektur- und Strukturaufgaben
- **CRT**: Core-Runtime-Aufgaben
- **HAL**: Hardware-Abstraktionsschicht
- **PRT**: Protokoll-/Transport-nahe Aufgaben
- **HWM**: Hardware-Mapping/Board-Setup
- **PROC**: Prozess-/Dokumentationsaufgaben

## Allgemeine Regeln je Task-Typ
- Scope strikt einhalten.
- Vorgaben aus Tasktext haben Vorrang.
- Abweichungen nur begründet und dokumentiert.

## Typische Schwerpunkte
- **RSK**: klare Verbote, Sicherheitsgrenzen, Nachvollziehbarkeit.
- **ARC**: Schichttrennung, Abhängigkeiten, Strukturkonsistenz.
- **CRT**: Runtime-Skeletons, Task-Start, deterministische Minimalität.
- **HAL**: nur Hardwareabstraktion, keine Fachlogik.
- **PRT**: Protokolllogik ohne Hardwarezugriff.
- **HWM**: Board-/Pin-/Profilzuordnung ohne fachliche Erweiterung.
- **PROC**: verbindliche, konsistente Prozessdokumentation.
