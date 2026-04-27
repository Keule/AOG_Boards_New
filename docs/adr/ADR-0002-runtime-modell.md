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
- Das System MUSS Work und Config unterstützen.

## Konsequenzen

- Moduswechsel MÜSSEN Task-Prioritäten beeinflussen.
- Moduswechsel MÜSSEN Task-Frequenzen beeinflussen.
- Moduswechsel MÜSSEN Suspend/Resume beeinflussen.
- Moduswechsel MÜSSEN Feature-Aktivierung beeinflussen.
