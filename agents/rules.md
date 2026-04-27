# Globale Regeln (VERPFLICHTEND)

## 1. Kein implizites Wissen

Agenten müssen davon ausgehen:
- kein Zugriff auf vorherige Chats
- kein Zugriff auf andere Repositories
- nur die aktuelle Aufgabenbeschreibung ist bekannt

## 2. Keine Interpretation

Agenten müssen:
- EXAKT umsetzen, was gefordert ist
- NICHT verbessern, erweitern oder optimieren
- NICHT „mitdenken", wenn es nicht explizit gefordert ist

## 3. Keine Überimplementierung

Wenn gefordert ist:
- Skeleton → nur Skeleton erstellen
- Stub → nur Stub erstellen

KEINE echte Funktionalität implementieren.

## 4. Schichten strikt einhalten

Transport:
- darf KEINE Protokolllogik enthalten

Protokoll:
- darf KEINEN Hardwarezugriff enthalten

HAL:
- darf KEINE Fachlogik enthalten

## 5. Dependency-Regel (KRITISCH)

Wenn eine Datei ein Header einer anderen Komponente inkludiert:

→ MUSS diese Komponente in REQUIRES stehen

## 6. Deterministisches Ergebnis

Agenten müssen:
- reproduzierbare Ergebnisse liefern
- keine zufälligen oder variierenden Lösungen erzeugen

## 7. Build muss funktionieren

Jede Umsetzung MUSS:
- kompilierbar sein
- vollständige Includes enthalten
- gültige CMake-Konfiguration haben

## 8. Kein Arduino

Verboten:
- Arduino.h
- WiFi.h
- WebServer.h
- SPI.h
- Wire.h
- BluetoothSerial.h

## 9. Minimalismus statt Cleverness

Bevorzugt:
- einfach
- explizit
- nachvollziehbar

Vermeiden:
- unnötige Abstraktion
- vorzeitige Architekturentscheidungen
- versteckte Logik
