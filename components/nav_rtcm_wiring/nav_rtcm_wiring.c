/* nav_rtcm_wiring.c — Pure function to wire RTCM outputs for all active GNSS receivers.
 *
 * This component contains NO HAL/UART/TCP/NTRIP code.  It only uses
 * board_profile queries and rtcm_router API calls.
 *
 * The function is extracted from app_core to be independently testable
 * via host tests (no ESP-IDF required). */

#include "nav_rtcm_wiring.h"

/* Stringification helpers for embedding constants in error messages. */
#define _NAV_XSTR(s) #s
#define NAV_XSTR(s) _NAV_XSTR(s)

nav_rtcm_wiring_result_t nav_rtcm_wire_outputs(
    const nav_rtcm_target_t* targets,
    int target_count,
    rtcm_router_t* router)
{
    nav_rtcm_wiring_result_t result = {
        .productive = false,
        .active_target_count = 0,
        .registered_output_count = 0,
        .error_detail = NULL
    };

    /* ---- Parameter validation ---- */
    if (targets == NULL || router == NULL) {
        result.error_detail = "invalid parameters: targets or router is NULL";
        return result;
    }

    if (target_count <= 0) {
        result.error_detail = "invalid parameters: target_count <= 0";
        return result;
    }

    /* ---- Iterate over all GNSS port candidates ---- */
    for (int i = 0; i < target_count; i++) {
        const nav_rtcm_target_t* t = &targets[i];

        /* Skip ports not active in this board/role profile */
        if (!board_profile_has_uart(t->port)) {
            continue;
        }

        /* This port is active — count it as a target */
        result.active_target_count++;

        /* FAIL: active GNSS receiver without RTCM TX path */
        if (!board_profile_has_uart_tx(t->port)) {
            result.error_detail =
                "NAV-RTCM-001 not productive: active GNSS receiver without RTCM TX path";
            return result;
        }

        /* FAIL: active GNSS receiver without wired TX buffer */
        if (t->tx_buffer == NULL) {
            result.error_detail =
                "NAV-RTCM-001 not productive: active GNSS receiver has no TX buffer";
            return result;
        }

        /* Register output — check return value.
         * Return -2 means router at capacity (RTCM_ROUTER_MAX_OUTPUTS exceeded).
         * Return -1 means NULL parameter (should not happen here). */
        int idx = rtcm_router_add_output(router, t->tx_buffer);
        if (idx < 0) {
            result.error_detail =
                "NAV-RTCM-001 not productive: rtcm_router_add_output() failed "
                "(router capacity " NAV_XSTR(RTCM_ROUTER_MAX_OUTPUTS)
                " exceeded by active GNSS receivers)";
            return result;
        }

        result.registered_output_count++;
    }

    /* ---- Post-iteration validation ---- */
    if (result.active_target_count == 0) {
        result.error_detail =
            "NAV-RTCM-001 not productive: no active GNSS receivers in profile";
        return result;
    }

    /* Safety net: registered must equal active (should never fail given
     * the early-return logic above, but guards against future changes) */
    if (result.registered_output_count != result.active_target_count) {
        result.error_detail =
            "NAV-RTCM-001 not productive: RTCM output count != active RTCM target count";
        return result;
    }

    result.productive = true;
    return result;
}
