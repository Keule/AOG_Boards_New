# Task-Typen (verbindlich)

## RSK
**Zweck**
- Regeln, Risiken, Safety-Grenzen und harte Verbote festlegen oder schärfen.

**Erlaubte Änderungen**
- Regeltexte, Sicherheitsvorgaben, prüfbare Constraints in Dokumentation.

**Verbotene Änderungen**
- funktionale Feature-Implementierungen ohne explizite Task-Freigabe.

**Typische Akzeptanzkriterien**
- Regeln sind eindeutig, testbar und widerspruchsfrei.
- Verbote und Muss-Regeln sind klar benannt.

## ARC
**Zweck**
- Architekturstruktur und Schichtgrenzen definieren oder präzisieren.

**Erlaubte Änderungen**
- Architektur-Dokumente, ADRs, Strukturvorgaben.

**Verbotene Änderungen**
- Implementierung außerhalb des expliziten ARC-Scopes.
- Schichtverletzungen durch implizite Abhängigkeiten.

**Typische Akzeptanzkriterien**
- Schichten und Verantwortlichkeiten sind klar.
- Dependencies sind nachvollziehbar und konsistent.

## CRT
**Zweck**
- Core-Runtime-Strukturen (z. B. task-Skeletons, Runtime-Startpfad) definieren/umsetzen.

**Erlaubte Änderungen**
- Runtime-bezogene Dateien laut Spec.
- minimale Skeleton-Implementierungen.

**Verbotene Änderungen**
- zusätzliche Scheduler-/Registry-Logik ohne Auftrag.
- HAL-/Hardware-/Protokollerweiterungen.

**Typische Akzeptanzkriterien**
- geforderte Runtime-Dateien exakt vorhanden.
- Aufrufreihenfolge und Task-Parameter entsprechen der Spec.

## HAL
**Zweck**
- Hardware-Abstraktionsschicht bereitstellen.

**Erlaubte Änderungen**
- HAL-Interfaces und HAL-Implementierungen im vorgegebenen Scope.

**Verbotene Änderungen**
- Fachlogik in HAL.
- Protokollentscheidungen in HAL.

**Typische Akzeptanzkriterien**
- HAL bleibt rein abstrahierend.
- keine Layer-Verletzung.

## PRT
**Zweck**
- Protokoll- und transportnahe Logik definieren/implementieren.

**Erlaubte Änderungen**
- Protokollparser, Framing, Zustandslogik, Transportadapter laut Spec.

**Verbotene Änderungen**
- direkter Hardwarezugriff in Protokollschicht.
- ungeforderte Architekturumbauten.

**Typische Akzeptanzkriterien**
- Protokollverhalten entspricht Spec.
- klare Trennung zu HAL und Fachlogik.

## HWM
**Zweck**
- Hardware-Mapping, Board-Profile, Pin-/Ressourcenzuordnung definieren.

**Erlaubte Änderungen**
- Mapping-Dateien und Boardprofile im Scope.

**Verbotene Änderungen**
- fachliche Features ohne Mapping-Bezug.
- implizite Architekturänderungen.

**Typische Akzeptanzkriterien**
- Mapping ist konsistent, reproduzierbar und vollständig.
- keine unbegründeten Seiteneffekte.

## PROC
**Zweck**
- Prozess-, Rollen-, Review- und Qualitätsregeln dokumentieren.

**Erlaubte Änderungen**
- Prozessdokumente in Markdown.
- Präzisierung vorhandener Regeln ohne Abschwächung.

**Verbotene Änderungen**
- Code-/Build-/Architekturänderungen.
- Aufweichen harter Regeln.

**Typische Akzeptanzkriterien**
- Dokumente sind vollständig, konsistent und auf Deutsch.
- Regeln sind verbindlich, prüfbar und widerspruchsfrei.
