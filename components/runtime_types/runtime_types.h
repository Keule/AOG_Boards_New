#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ---- Fast Cycle Context (Core 1 / task_fast only) ----
 *
 * Passed to all fast_input/fast_process/fast_output callbacks.
 * Contains NO service_profile — that is Core-0 Service-Control-State
 * managed by the per-group service tasks.                            */

typedef struct {
    uint64_t cycle_id;
    uint64_t timestamp_us;
    uint32_t period_us;
    uint32_t last_cycle_duration_us;
    uint32_t worst_cycle_duration_us;
} fast_cycle_context_t;

/* ---- Service Group Assignment (Core 0) ----
 *
 * Each service_step component MUST be assigned to exactly one group.
 * The runtime creates one FreeRTOS task per group, pinned to Core 0.
 * Groups may have different priorities, periods, and suspend state
 * depending on the active Work/Config mode.                         */

typedef enum {
    SERVICE_GROUP_UART = 0,        /* UART transport service task */
    SERVICE_GROUP_UDP,             /* UDP transport service task */
    SERVICE_GROUP_TCP_NTRIP,       /* TCP + NTRIP client service task */
    SERVICE_GROUP_DIAGNOSTICS,     /* Diagnostics / health monitoring */
    SERVICE_GROUP_COUNT            /* Must be last */
} service_group_t;

/* ---- Service Profile (Core-0 Service-Control-State) ----
 *
 * Controls priority, period, and suspend state of a service task group.
 * In Work mode and Config mode, profiles may differ.
 *
 * TODO (nächster Schritt): Suspend/Resume ist aktuell als Profil-Flag
 * implementiert, aber die Tasks setzen ihr suspended-Flag nur beim
 * Profilwechsel, nicht per explizitem API-Aufruf.  Produktives
 * Suspend/Resume erfordert zusaetzliche API-Funktionen und sollte
 * als eigener Follow-up umgesetzt werden.                          */

#define SERVICE_PROFILE_PRIORITY_DEFAULT   5
#define SERVICE_PROFILE_PERIOD_MS_DEFAULT  10

typedef struct {
    uint8_t  priority;      /* FreeRTOS task priority (1..configMAX_PRIORITIES) */
    uint16_t period_ms;     /* Task loop interval in milliseconds */
    bool     suspended;     /* If true, service step is skipped (task still exists) */
} service_profile_t;

#define SERVICE_PROFILE_DEFAULT() { \
    .priority   = SERVICE_PROFILE_PRIORITY_DEFAULT, \
    .period_ms  = SERVICE_PROFILE_PERIOD_MS_DEFAULT, \
    .suspended  = false \
}

/* ---- System Mode (Work / Config) ----
 *
 * Work  = produktiver Betrieb.  UART/UDP/TCP_NTRIP aktiv,
 *          Diagnostics langsam / niedrig priorisiert.
 * Config = Konfigurationsmodus.  UART/UDP/TCP_NTRIP aktiv,
 *          Diagnostics schneller / hoeher priorisiert.
 *
 * Modi wirken sich NUR auf Core-0-Service-Tasks aus (via service_profile).
 * task_fast / Core 1 bleibt mode-frei und unveraendert.                 */

typedef enum {
    SYSTEM_MODE_WORK = 0,
    SYSTEM_MODE_CONFIG,
    SYSTEM_MODE_COUNT
} system_mode_t;
