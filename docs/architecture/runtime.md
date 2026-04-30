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
- task_fast ist mode-frei (kein Work/Config-Einfluss auf Priorität/Frequenz).
- FastCycleContext enthält KEIN service_profile.

## Core 0

- Core 0 MUSS mehrere Service-Tasks verwenden.
- Core 0 DARF KEINEN eigenen Scheduler implementieren.
- Core 0 MUSS FreeRTOS verwenden.
- Core 0 MUSS pro I/O-Fläche einen dedizierten Service-Task haben:
  - uart_service_task (SERVICE_GROUP_UART)
  - udp_service_task (SERVICE_GROUP_UDP)
  - tcp_ntrip_service_task (SERVICE_GROUP_TCP_NTRIP)
  - diagnostics_service_task (SERVICE_GROUP_DIAGNOSTICS)
- Jeder Service-Task MUSS auf Core 0 gepinnt sein.
- Jeder Service-Task wird durch ein service_profile gesteuert
  (Priorität, Periode, Suspend/Resume).
- service_profile ist Core-0-Service-Control-State und gehört
  NICHT zum FastCycleContext.
- Service-Tasks lesen Profilwerte zur Laufzeit aus zentralem Zustand
  (s_profiles[]), nicht aus einer Cached-Kopie vom Task-Start.

## Modi (Work / Config)

- Das System unterstützt zwei Modi: Work und Config.
- Modi wirken sich NUR auf Core-0-Service-Tasks aus (via service_profile).
- task_fast / Core 1 bleibt mode-frei und unverändert.
- Standardmodus bei Init ist Work.

### Moduswechsel (runtime_set_system_mode)

- runtime_set_system_mode() ist produktiv implementiert.
- Ungültige Modi (< 0 oder >= SYSTEM_MODE_COUNT) werden abgelehnt (return -1).
- Der aktuelle Modus ist abrufbar via runtime_get_system_mode().
- Profil-Periode aendert sich live (naechster Loop).  Profil-Prioritaet
  wird in s_profiles[] geschrieben, aber FreeRTOS-Prioritaet ist TODO.

### Profil-Differenzierung

- UART, UDP, TCP_NTRIP sind in Work und Config aktiv (nicht suspended).
- Diagnostics in Work:   priority=3, period=100ms (langsam, niedrig priorisiert).
- Diagnostics in Config: priority=6, period=50ms  (schneller, hoeher priorisiert).

### Aktuell produktiv

- Prioritäten- und Periodenwechsel via runtime_set_system_mode() schreiben
  in den zentralen Profilzustand (s_profiles[]).
- **Periodenwechsel** wirkt sofort beim naechsten Loop-Iteration des
  jeweiligen Service-Task (live, kein Neustart noetig).
- **Prioritaetswerte** werden korrekt in s_profiles[] geschrieben, aber
  die FreeRTOS-Task-Prioritaet wird **noch nicht live** aktualisiert.
  Die Prioritaet aendert sich erst nach einem Neustart der Tasks
  (runtime_start()).
- TODO: vTaskPrioritySet() fuer Priority-Wechsel ohne Neustart —
  Task-Handles muessen dafuer gespeichert werden.
  Bis dahin: Prioritaetswechsel erfordert runtime_start() Neustart.

### Noch nicht produktiv (TODO)

- Explizites Suspend/Resume-API einzelner Service-Gruppen (z.B.
  `runtime_suspend_group()` / `runtime_resume_group()`).  Das
  `suspended`-Flag im service_profile ist vorhanden, aber ein Moduswechsel
  aendert aktuell nicht den Suspended-Zustand.
- Feature-Aktivierung je Modus (z.B. bestimmtes Modul nur in Config
  sichtbar).  Die Infrastruktur dafuer ist nicht implementiert.
