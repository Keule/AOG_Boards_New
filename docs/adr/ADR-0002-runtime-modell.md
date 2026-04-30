# ADR-0002 Runtime-Modell

## Status

- Status MUSS Accepted sein.

## Kontext

- task_fast MUSS deterministisch laufen.
- task_fast MUSS einen festen Zyklus haben.
- task_fast DARF NICHT blockieren.
- task_fast DARF KEIN Netzwerk verwenden.
- Core 0 MUSS mehrere Service-Tasks verwenden.
- Core 0 MUSS FreeRTOS verwenden.

## Entscheidung

- Core 1 MUSS task_fast mit Ablauf fast_input, fast_process, fast_output ausführen.
- task_fast DARF NUR Device-SPI verwenden.
- task_fast DARF NICHT Ethernet-SPI verwenden.
- Core 0 MUSS Service-Tasks unter FreeRTOS ausführen.
- Core 0 DARF KEINEN eigenen Scheduler implementieren.
- Core 0 MUSS pro I/O-Fläche einen dedizierten Service-Task haben.
- Das System MUSS Work und Config unterstützen.

## service_profile

- service_profile ist Core-0-Service-Control-State.
- service_profile gehört NICHT zum FastCycleContext.
- service_profile steuert Priorität, Periode und Suspend/Resume
  der Core-0-Service-Tasks.
- Work/Config-Moduswechsel wirken über service_profile auf die
  Core-0-Service-Tasks, nicht auf task_fast.
- Service-Tasks lesen Profilwerte zur Laufzeit aus zentralem Zustand
  (s_profiles[]), sodass Periodenwechsel sofort wirksam werden.
  Prioritaetswechsel erfordern noch vTaskPrioritySet() (TODO).

## Umsetzungsstand

### Produktiv

- runtime_set_system_mode() wechselt zwischen Work/Config und aktualisiert
  alle vier Service-Profile.
- Periode aendert sich sofort beim naechsten Loop.
- Ungueltige Modi werden abgelehnt.
- Standardmodus ist Work.

### TODO (nächster Schritt)

- vTaskPrioritySet() fuer Priority-Wechsel ohne Neustart (Task-Handles
  muessen gespeichert werden).
- Explizites Suspend/Resume-API fuer einzelne Service-Gruppen.
- Feature-Aktivierung je Modus.

## Konsequenzen

- Moduswechsel SOLLEN Task-Prioritäten beeinflussen.
  (TODO: vTaskPrioritySet — aktuell nur Profil-Wert geschrieben,
   nicht als FreRTOS-Prio live angewendet)
- Moduswechsel beeinflussen Task-Frequenzen (Periode, produktiv).
- Moduswechsel SOLLEN Suspend/Resume beeinflussen.
  (TODO: noch nicht als explizites API umgesetzt)
- Moduswechsel SOLLEN Feature-Aktivierung beeinflussen.
  (TODO: noch nicht implementiert)
- task_fast/Core 1 bleibt mode-frei.
