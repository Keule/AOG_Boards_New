# Workflow für agentengestützte Entwicklung (verbindlich)

## Zweck
Dieses Dokument definiert den verbindlichen Standardablauf für Aufgaben im Repository.

## Grundprozess
1. **Planung**
2. **Implementierung**
3. **Review**
4. **Entscheidung**

Der Ablauf ist strikt sequenziell: Erst wenn ein Schritt abgeschlossen ist, beginnt der nächste.

## Rollen
- **Planer-Agent**
  - erstellt eine vollständige, eigenständige Task-Spec,
  - definiert Scope, Dateien, harte Constraints und Akzeptanzkriterien.
- **Entwickler-Agent**
  - setzt die Spec exakt um,
  - erweitert Scope nicht,
  - trifft keine neuen Architekturentscheidungen.
- **Reviewer- + Nacharbeitsplaner-Agent**
  - prüft strikt gegen die Spec,
  - entscheidet **ACCEPTED** oder **REJECTED**,
  - formuliert bei Bedarf eine Nacharbeit oder Neuaufgabe.

## Entscheidung im Review
Die Entscheidung muss exakt eine der folgenden sein:
- **ACCEPTED**
- **REJECTED**

## Verhalten bei REJECTED
Bei **REJECTED** sind genau zwei Wege erlaubt:
1. **vollständige Neuaufgabe**, wenn Abweichung groß/strukturell ist,
2. **klar begrenzte Nacharbeit**, wenn Fehler klein und präzise korrigierbar ist.

## Regeln für Nacharbeit
Nacharbeit darf ausschließlich konkrete Review-Mängel beheben.

Nacharbeit darf **nicht**:
- Scope erweitern,
- Architektur ändern,
- zusätzliche Features einführen,
- neue Nebenbaustellen öffnen.

## No-Drift-Regel
Plan, Implementierung und Review müssen auf derselben Spec basieren.
Abweichungen ohne dokumentierte Begründung gelten als Prozessfehler.
