#pragma once
/* ========================================================================
 * nav_recovery.h — Recovery-Regeln Koordinator (NAV-DIAG-001 Teil 3)
 *
 * Dokumentiert und formalisiert die Recovery-Regeln, die bereits in den
 * einzelnen Subsystemen implementiert sind. Bietet eine zentrale
 * Prüffunktion, die den Systemzustand evaluiert und Recovery-Aktionen
 * empfiehlt.
 *
 * KEINE eigenen Reconnect-Implementierungen (die leben in ntrip_client,
 * transport_tcp, etc.). Dieser Koordinator liest nur Status und gibt
 * Empfehlungen/bewertet den Zustand.
 *
 * Recovery-Regeln:
 *   1. NTRIP reconnect/backoff: ntrip_client RETRY_WAIT → CONNECTING
 *   2. TCP reconnect: transport_tcp disconnect detection → reconnect
 *   3. Ethernet link down/up: hal_eth_is_connected() → re-init
 *   4. UART Fehlerzähler: transport_uart stats → health evaluation
 *   5. GNSS freshness timeout: gnss_um980 snapshot.fresh → false
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include "nav_health.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Recovery Action Flags ---- */

typedef enum {
    NAV_RECOVERY_NONE = 0,
    NAV_RECOVERY_NTRIP_RECONNECT = (1 << 0),  /* NTRIP should reconnect */
    NAV_RECOVERY_TCP_RECONNECT   = (1 << 1),  /* TCP connection lost */
    NAV_RECOVERY_ETH_REINIT      = (1 << 2),  /* Ethernet link went down */
    NAV_RECOVERY_GNSS_RESET      = (1 << 3),  /* GNSS needs attention */
    NAV_RECOVERY_UART_CHECK      = (1 << 4),  /* UART errors detected */
} nav_recovery_flags_t;

/* ---- Recovery Status ---- */

typedef struct {
    nav_recovery_flags_t actions;     /* recommended recovery actions */
    bool ntrip_should_reconnect;      /* NTRIP in ERROR/RETRY_WAIT state */
    bool tcp_disconnected;            /* TCP not connected */
    bool eth_link_down;               /* Ethernet link is down */
    bool gnss_primary_stale;          /* Primary GNSS data is stale */
    bool gnss_secondary_stale;        /* Secondary GNSS data is stale */
    bool uart_primary_errors;         /* Primary UART has errors */
    bool uart_secondary_errors;       /* Secondary UART has errors */
    uint32_t ntrip_consecutive_errors; /* NTRIP consecutive error count */
} nav_recovery_status_t;

/* ---- API ---- */

/* Evaluate current recovery status from health snapshot.
 * Checks all subsystems and sets appropriate action flags.
 * Non-blocking, no side effects. */
void nav_recovery_evaluate(const nav_health_snapshot_t* health,
                            nav_recovery_status_t* recovery);

/* Check if any recovery action is recommended. */
bool nav_recovery_needs_action(const nav_recovery_status_t* recovery);

/* Get human-readable description of recovery flags. */
const char* nav_recovery_flag_name(nav_recovery_flags_t flag);

#ifdef __cplusplus
}
#endif
