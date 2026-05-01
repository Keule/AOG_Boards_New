/* nav_recovery.c — Recovery Rule Evaluator (NAV-DIAG-001 Teil 3)
 *
 * Evaluates the health snapshot and recommends recovery actions.
 * Does NOT perform any recovery itself — just reads state and advises.
 * Non-blocking, no side effects, safe to call from any context.
 */

#include "nav_recovery.h"
#include <string.h>

void nav_recovery_evaluate(const nav_health_snapshot_t* health,
                            nav_recovery_status_t* recovery)
{
    if (recovery == NULL) { return; }
    memset(recovery, 0, sizeof(nav_recovery_status_t));

    if (health == NULL) { return; }

    /* ---- NTRIP: reconnect if in ERROR or RETRY_WAIT ---- */
    recovery->ntrip_should_reconnect =
        (health->ntrip_state == NAV_NTRIP_STATE_ERROR ||
         health->ntrip_state == NAV_NTRIP_STATE_RETRY_WAIT);

    if (recovery->ntrip_should_reconnect) {
        recovery->actions |= NAV_RECOVERY_NTRIP_RECONNECT;
        recovery->ntrip_consecutive_errors = health->ntrip_reconnect_count;
    }

    /* ---- TCP: flag if disconnected (NTRIP depends on TCP) ---- */
    recovery->tcp_disconnected = !health->tcp_connected;
    if (recovery->tcp_disconnected) {
        recovery->actions |= NAV_RECOVERY_TCP_RECONNECT;
    }

    /* ---- Ethernet: flag if link is down ---- */
    recovery->eth_link_down = !health->eth_link_up;
    if (recovery->eth_link_down) {
        recovery->actions |= NAV_RECOVERY_ETH_REINIT;
    }

    /* ---- GNSS Primary: stale or invalid ---- */
    recovery->gnss_primary_stale =
        (!health->gnss_primary_fresh && health->gnss_primary_bytes > 0);

    /* ---- GNSS Secondary: stale or invalid ---- */
    recovery->gnss_secondary_stale =
        (!health->gnss_secondary_fresh && health->gnss_secondary_bytes > 0);

    /* GNSS needs attention if stale and has received data before
     * (bytes > 0 means it was working at some point) */
    if (recovery->gnss_primary_stale || recovery->gnss_secondary_stale) {
        recovery->actions |= NAV_RECOVERY_GNSS_RESET;
    }

    /* ---- UART: check for errors ---- */
    recovery->uart_primary_errors =
        (health->uart_primary_rx_overflows > 0 ||
         health->uart_primary_tx_errors > 0);
    recovery->uart_secondary_errors =
        (health->uart_secondary_rx_overflows > 0 ||
         health->uart_secondary_tx_errors > 0);

    if (recovery->uart_primary_errors || recovery->uart_secondary_errors) {
        recovery->actions |= NAV_RECOVERY_UART_CHECK;
    }
}

bool nav_recovery_needs_action(const nav_recovery_status_t* recovery)
{
    if (recovery == NULL) { return false; }
    return recovery->actions != NAV_RECOVERY_NONE;
}

const char* nav_recovery_flag_name(nav_recovery_flags_t flag)
{
    switch (flag) {
        case NAV_RECOVERY_NTRIP_RECONNECT: return "ntrip_reconnect";
        case NAV_RECOVERY_TCP_RECONNECT:   return "tcp_reconnect";
        case NAV_RECOVERY_ETH_REINIT:      return "eth_reinit";
        case NAV_RECOVERY_GNSS_RESET:      return "gnss_reset";
        case NAV_RECOVERY_UART_CHECK:      return "uart_check";
        default:                           return "none";
    }
}
