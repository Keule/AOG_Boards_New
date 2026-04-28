# Definition of Done (DoD)

## Global
Eine Aufgabe ist nur dann "Done", wenn:
- alle geforderten Dateien im Sollzustand vorliegen,
- keine unerlaubten Zusatzänderungen enthalten sind,
- Review ohne offene Abweichung abgeschlossen ist.

## DoD je Task-Typ
### RSK
- Sicherheits-/Regelvorgaben vollständig umgesetzt.
- Keine unkontrollierten Seiteneffekte.

### ARC
- Zielarchitektur gemäß Vorgabe umgesetzt.
- Schicht- und Dependency-Regeln eingehalten.

### CRT
- Runtime-Struktur exakt wie spezifiziert.
- Keine zusätzliche Scheduler-/Registry-Logik.
- Task-Parameter exakt gemäß Vorgabe.

### HAL
- HAL-API korrekt, ohne Fachlogik.
- Kein Protokollcode in HAL.

### PRT
- Protokolllogik vollständig, ohne Hardwarezugriff.
- Schnittstellen zu anderen Schichten klar.

### HWM
- Hardware-Mapping korrekt und reproduzierbar.
- Keine fachliche Erweiterung außerhalb Mapping/Profil.

### PROC
- Dokumente vollständig vorhanden.
- Inhalte konsistent, widerspruchsfrei und auf Deutsch.
