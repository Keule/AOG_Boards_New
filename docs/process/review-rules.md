# Review-Regeln (verbindlich)

## Grundsatz
Review prüft strikt gegen die Task-Spec (Soll/Ist-Vergleich).

## Was Review prüfen muss
- harte Constraints,
- exakte Dateivorgaben,
- Akzeptanzkriterien,
- Scope-Treue,
- Regelkonformität (`AGENTS.md`, `agents/*`, Prozessdokumente).

## Was Review nicht prüfen darf
- persönlicher Stil,
- subjektive Präferenzen ohne Spec-Bezug,
- ungeforderte Architektur-Neuerfindung.

## Entscheidungspflicht
Der Reviewer muss genau eine Entscheidung ausgeben:
- **ACCEPTED**
- **REJECTED**

## Pflichtinhalt bei REJECTED
Bei **REJECTED** müssen zwingend enthalten sein:
1. konkrete Abweichungen (Soll vs. Ist),
2. **Root Cause**,
3. **Lessons Learned**,
4. Entscheidung: Neuaufgabe oder klar begrenzte Nacharbeit.

## Nacharbeits-Empfehlung
Nacharbeit darf nur empfohlen werden, wenn der Fehler:
- klein,
- klar begrenzt,
- ohne Architekturänderung korrigierbar ist.
