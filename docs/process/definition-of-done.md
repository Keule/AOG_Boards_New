# Definition of Done (verbindlich)

## 1) Allgemeine Tasks
Eine Aufgabe ist nur dann abgeschlossen, wenn:
- vollständige Umsetzung der Spec vorliegt,
- alle geforderten Dateien vorhanden sind,
- alle geforderten Inhalte im Sollzustand vorliegen,
- alle Akzeptanzkriterien erfüllt sind.

## 2) Build-/Skeleton-Tasks
Zusätzlich MUSS gelten:
- Build ist erfolgreich, wenn Code betroffen ist.
- keine verbotenen Includes.
- keine Layerverletzung.
- keine Überimplementierung.

## 3) Architektur-/Doku-Tasks
Zusätzlich MUSS gelten:
- vollständige Umsetzung der dokumentierten Anforderungen.
- keine Abschwächung bestehender Regeln.
- keine implizite Architekturänderung.
- Spec vollständig erfüllt.

## 4) Code-/Runtime-Tasks
Zusätzlich MUSS gelten:
- Build ist erfolgreich, wenn Code betroffen ist.
- keine verbotenen Includes.
- keine Layerverletzung.
- keine Überimplementierung.
- CMake- und Dependency-Angaben sind vollständig.
- Spec vollständig erfüllt.

## 5) Review-/Nacharbeits-Tasks
Zusätzlich MUSS gelten:
- Review enthält zwingend **ACCEPTED** oder **REJECTED**.
- bei **REJECTED** sind Root Cause und Lessons Learned enthalten.
- Nacharbeit behebt nur benannte Mängel.
- Nacharbeit erweitert Scope nicht.
- Spec vollständig erfüllt.
