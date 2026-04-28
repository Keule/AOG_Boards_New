# Task-Typen (verbindlich)

## RSK
### Zweck
Regeln, Risiken und Sicherheitsvorgaben verbindlich festlegen oder schärfen.

### Erlaubte Änderungen
- Regeltexte und Sicherheitsvorgaben im vorgegebenen Scope.
- Präzisierungen, die Prüfbarkeit und Eindeutigkeit erhöhen.

### Verbotene Änderungen
- Feature-Implementierungen ohne explizite Freigabe.
- Scope-Erweiterungen außerhalb der Spec.

### Typische Akzeptanzkriterien
- Regeln sind eindeutig, prüfbar und widerspruchsfrei.
- Verbote und Muss-Anforderungen sind explizit formuliert.

## ARC
### Zweck
Architekturstruktur, Schichten und Verantwortlichkeiten verbindlich definieren.

### Erlaubte Änderungen
- Architektur- und ADR-Dokumentation im expliziten Scope.
- Präzisierung von Schichtgrenzen und Abhängigkeitsregeln.

### Verbotene Änderungen
- Implementierung außerhalb des ARC-Auftrags.
- Verdeckte Architekturänderungen ohne Spec.

### Typische Akzeptanzkriterien
- Schichtgrenzen sind eindeutig dokumentiert.
- Abhängigkeiten sind konsistent und nachvollziehbar.

## CRT
### Zweck
Core-Runtime-Strukturen gemäß Spec erstellen oder korrigieren.

### Erlaubte Änderungen
- Runtime-Dateien und Runtime-Skeletons im freigegebenen Scope.
- Task-Startpfade und minimale Runtime-Aufrufe gemäß Vorgabe.

### Verbotene Änderungen
- zusätzliche Scheduler-/Registry-Logik ohne Auftrag.
- HAL-/Hardware-/Protokoll-Logik außerhalb CRT-Scope.

### Typische Akzeptanzkriterien
- alle geforderten CRT-Dateien liegen exakt vor.
- Aufrufreihenfolge, Delay- und Task-Parameter entsprechen der Spec.

## HAL
### Zweck
Hardware-Abstraktionsschicht strikt getrennt von Fachlogik umsetzen.

### Erlaubte Änderungen
- HAL-Interfaces und HAL-Implementierungen laut Task-Spec.
- Board- und Gerätezugriffe ausschließlich über HAL-Ebene.

### Verbotene Änderungen
- Fachlogik in HAL-Dateien.
- Protokollentscheidungen in HAL.

### Typische Akzeptanzkriterien
- HAL ist rein abstrahierend.
- Keine Layerverletzung zwischen HAL und Protokoll/Fachlogik.

## PRT
### Zweck
Protokoll- und transportnahe Logik gemäß Spezifikation umsetzen.

### Erlaubte Änderungen
- Parser-, Framing- und Zustandslogik im PRT-Scope.
- Transportkopplung entsprechend den vorgegebenen Schnittstellen.

### Verbotene Änderungen
- direkter Hardwarezugriff in Protokolldateien.
- ungeforderte Architektur- oder Layer-Änderungen.

### Typische Akzeptanzkriterien
- Protokollverhalten entspricht der Spec.
- Trennung zu HAL und Fachlogik ist eingehalten.

## HWM
### Zweck
Hardware-Mapping und Boardzuordnung reproduzierbar festlegen.

### Erlaubte Änderungen
- Mapping-Dateien, Pin-Zuordnungen und Boardprofile im Scope.
- Korrekturen an Mapping-Definitionen gemäß Task-Spec.

### Verbotene Änderungen
- fachliche Features ohne Mapping-Bezug.
- Architekturänderungen außerhalb HWM-Scope.

### Typische Akzeptanzkriterien
- Mapping ist vollständig und konsistent.
- Zuordnungen sind reproduzierbar und eindeutig.

## PROC
### Zweck
Prozessregeln, Review-Abläufe und Qualitätskriterien verbindlich dokumentieren.

### Erlaubte Änderungen
- Prozessdokumente in Markdown im vorgegebenen Pfad.
- Präzisierungen ohne Abschwächung bestehender Regeln.

### Verbotene Änderungen
- Code-, Build- oder Architekturänderungen.
- Aufweichen harter Regeln.

### Typische Akzeptanzkriterien
- Dokumente sind vollständig, konsistent und deterministisch.
- Regeln sind verbindlich, prüfbar und widerspruchsfrei.
