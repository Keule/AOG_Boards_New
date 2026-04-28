# Workflow (verbindlich)

## Ziel
Dieser Workflow beschreibt den verbindlichen Ablauf für agentengestützte Aufgaben im Repository.

## Prozess
1. **Plan**
   - Planner analysiert die Aufgabe.
   - Planner definiert Scope, betroffene Dateien und Prüfschritte.
2. **Implementierung**
   - Developer setzt die Aufgabe exakt gemäß Plan und Task-Vorgaben um.
   - Keine Scope-Erweiterung ohne explizite Begründung.
3. **Review**
   - Reviewer vergleicht Soll/Ist anhand Tasktext, Dateivorgaben und Regeln.
   - Review erfolgt dateibezogen und nachvollziehbar.
4. **Accept / Reject**
   - **Accept**, wenn alle Akzeptanzkriterien erfüllt sind.
   - **Reject**, wenn Abweichungen bestehen.
5. **Nacharbeit (falls Reject)**
   - Nacharbeitsplan mit minimalem Scope.
   - Danach erneuter Soll/Ist-Review.

## Rollen
- **Planner**: erstellt klaren, prüfbaren Umsetzungsplan.
- **Developer**: implementiert exakt und minimal.
- **Reviewer / Nacharbeitsplaner**: prüft, entscheidet Accept/Reject und formuliert ggf. Nacharbeit.

## No-Drift-Regel
Zwischen Plan, Implementierung und Review darf kein inhaltlicher Drift entstehen.
Änderungen außerhalb des freigegebenen Scopes sind zu markieren und zu begründen.
