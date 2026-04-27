# Runtime-Modell

## Core 1

- task_fast MUSS deterministisch laufen.
- task_fast MUSS einen festen Zyklus haben.
- task_fast MUSS folgenden Ablauf haben:
  - fast_input
  - fast_process
  - fast_output
- task_fast DARF NICHT blockieren.
- task_fast DARF KEIN Netzwerk verwenden.
- task_fast DARF NUR Device-SPI verwenden.
- task_fast DARF NICHT Ethernet-SPI verwenden.

## Core 0

- Core 0 MUSS mehrere Service-Tasks verwenden.
- Core 0 DARF KEINEN eigenen Scheduler implementieren.
- Core 0 MUSS FreeRTOS verwenden.

## Modi

- Das System MUSS zwei Modi unterstützen:
  - Work
  - Config
- Diese Modi MÜSSEN Task-Prioritäten beeinflussen.
- Diese Modi MÜSSEN Task-Frequenzen beeinflussen.
- Diese Modi MÜSSEN Suspend/Resume beeinflussen.
- Diese Modi MÜSSEN Feature-Aktivierung beeinflussen.
