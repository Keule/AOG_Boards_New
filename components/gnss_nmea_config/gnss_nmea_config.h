#pragma once
/* ========================================================================
 * gnss_nmea_config.h — UM980 NMEA Configuration Manager
 *                        (NAV-UART-STABILIZING-R1)
 *
 * Sends LOG/UNLOG commands to UM980 receivers via UART TX.
 * Reads responses from shared UART RX buffer during restore.
 * Provides idempotent NMEA restore with robust ACK detection
 * and POST-RESTORE effect verification.
 *
 * Architecture:
 *   - During restore, this component intercepts the UART RX buffer
 *     before gnss_um980 reads it. NMEA data is forwarded via
 *     gnss_um980_feed() so parsing continues uninterrupted.
 *   - After restore, effect verification runs for VERIFY_WINDOW_MS.
 *   - After verification, component goes IDLE permanently.
 *
 * HARD RULES:
 *   - cmd_index is incremented in EXACTLY ONE place (process_cmd_result)
 *   - No global state between receivers
 *   - No network code
 *   - No blocking UART reads
 *   - ESP-IDF/PlatformIO only
 *   - NTRIP/RTCM/PGN214 must not regress
 *
 * IMPORTANT: runtime_component_t MUST be the first field for safe casting.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "runtime_component.h"
#include "transport_uart.h"
#include "gnss_um980.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NOTE: transport_uart_t and gnss_um980_t are anonymous-struct typedefs
 * and cannot be forward-declared. Including the headers is safe since
 * there is no circular dependency between these components. */

/* ---- UM980 Command/Response Constants ---- */

#define UM980_CMD_MAX_LEN        80
#define UM980_RESP_MAX_LEN       512
#define UM980_CMD_QUEUE_MAX      20   /* enough for UNLOG all + LOG target */
#define UM980_RESP_TIMEOUT_MS    500
#define UM980_INTER_CMD_DELAY_MS 200

/* Effect verification window after restore completes */
#define UM980_VERIFY_WINDOW_MS   8000  /* 8 seconds observation */

/* ---- Command Result Status ---- */

typedef enum {
    UM980_CMD_IDLE = 0,        /* No command in progress */
    UM980_CMD_SENDING,         /* Attempting to send */
    UM980_CMD_SENT,            /* Sent, collecting response */
    UM980_CMD_OK,              /* "OK" found in response */
    UM980_CMD_ERROR,           /* "ERROR" found in response */
    UM980_CMD_TIMEOUT,         /* No response within timeout */
    UM980_CMD_TRANSPORT_FAIL,  /* Could not write to TX buffer */
    UM980_CMD_DONE             /* Command finalized (single exit point) */
} um980_cmd_status_t;

/* ---- Restore State ---- */

typedef enum {
    NMEA_RESTORE_IDLE = 0,
    NMEA_RESTORE_UNLOGGING,
    NMEA_RESTORE_SETTING,
    NMEA_RESTORE_VERIFYING,    /* NEW: post-restore effect check */
    NMEA_RESTORE_DONE,
    NMEA_RESTORE_FAILED
} nmea_restore_state_t;

/* ---- Restore Effect Flags ---- */

typedef enum {
    RESTORE_EFFECT_NONE       = 0,
    RESTORE_EFFECT_ACK_OK                      = (1 << 0),
    RESTORE_EFFECT_ACK_MISSING_BUT_EFFECT_OK  = (1 << 1),
    RESTORE_EFFECT_ACK_OK_BUT_EFFECT_BAD      = (1 << 2),
    RESTORE_EFFECT_TIMEOUT                     = (1 << 3),
    RESTORE_EFFECT_GSV_STILL_ACTIVE            = (1 << 4),
    RESTORE_EFFECT_GST_NOT_AVAILABLE          = (1 << 5),
    RESTORE_EFFECT_RATE_TOO_HIGH              = (1 << 6),
    RESTORE_EFFECT_RATE_TOO_LOW               = (1 << 7),
    RESTORE_EFFECT_UART_OVERRUN               = (1 << 8),
    RESTORE_EFFECT_RING_FULL                  = (1 << 9),
    RESTORE_EFFECT_GSA_HIGH                   = (1 << 10),
    /* R3 B1: New critical failure flags */
    RESTORE_EFFECT_NO_NMEA_OUTPUT             = (1 << 11),
    RESTORE_EFFECT_GGA_MISSING                = (1 << 12),
    RESTORE_EFFECT_RMC_MISSING                = (1 << 13),
    RESTORE_EFFECT_GSA_MISSING                = (1 << 14),
    RESTORE_EFFECT_GSA_MULTI_TALKER            = (1 << 15),
    RESTORE_EFFECT_GST_UNEXPECTED              = (1 << 16),
    RESTORE_EFFECT_GST_DISABLED                = (1 << 17),
} restore_effect_flag_t;

/* ---- Command Result ---- */

typedef struct {
    um980_cmd_status_t status;
    bool    transport_ok;
    bool    response_received;
    bool    ack_ok;
    bool    ack_error;
    char    response_excerpt[128];
    uint16_t response_len;
    uint64_t started_us;
    uint64_t completed_us;
} um980_cmd_result_t;

/* ---- Target NMEA Profile ---- */

typedef struct {
    float    gga_period_s;     /* 0 = disabled, e.g. 0.1 = 10 Hz */
    float    rmc_period_s;
    float    gst_period_s;     /* 0 = disabled/GST_NOT_AVAILABLE */
    float    gsa_period_s;
    float    gsv_period_s;     /* 0 = disabled */
} nmea_profile_t;

/* Conservative stable defaults */
#define NMEA_PROFILE_STABLE { \
    .gga_period_s = 0.02f,  /* 10 Hz */ \
    .rmc_period_s = 0.05f,  /* 10 Hz */ \
    .gst_period_s = 1.0f,  /* disabled — GST_NOT_AVAILABLE */ \
    .gsa_period_s = 1.0f,  /*  1 Hz */ \
    .gsv_period_s = 0.0f   /* disabled */ \
}

/* UM980 port identifier */
#define UM980_PORT_COM2  "COM2"

/* ---- NMEA Config Instance ---- */

typedef struct gnss_nmea_config_t {
    runtime_component_t component;    /* MUST be first */

    /* Identity */
    uint8_t     instance_id;
    const char* name;

    /* ---- External references (set by app_core, NOT owned) ---- */
    transport_uart_t*      uart;
    gnss_um980_t*          gnss;

    /* ---- Restore state machine ---- */
    nmea_restore_state_t restore_state;
    uint8_t  cmd_index;
    uint8_t  cmd_count;
    char     cmd_queue[UM980_CMD_QUEUE_MAX][UM980_CMD_MAX_LEN];
    uint64_t cmd_sent_us;
    uint64_t last_cmd_us;
    uint64_t restore_start_us;

    /* ---- Response capture ---- */
    char     response_buf[UM980_RESP_MAX_LEN];
    uint16_t response_pos;

    /* ---- Current command result ---- */
    um980_cmd_result_t last_result;

    /* ---- Overall restore results ---- */
    uint8_t restore_ok_count;
    uint8_t restore_fail_count;
    char    restore_fail_cmd[UM980_CMD_MAX_LEN];

    /* ---- Target profile ---- */
    nmea_profile_t target_profile;

    /* ---- UM980 port for commands (Aufgabe 3: per-receiver) ---- */
    char     um980_port[8];       /* "COM1", "COM2", etc. default: "COM2" */

    /* ---- Effect verification ---- */
    uint64_t       verify_start_us;
    uint32_t       effect_flags;            /* bitmask of restore_effect_flag_t */
    uint32_t       verify_overruns_start;   /* uart overruns at verify start */
    uint32_t       verify_overruns_end;     /* uart overruns at verify end */
    bool           gsv_was_disabled;         /* true if GSV rate == 0 during verify */
    bool           gst_is_available;         /* true if GST rate > 0 during verify */
    float          verify_gsv_hz;
    float          verify_gga_hz;
    float          verify_rmc_hz;
    float          verify_gsa_hz;
    float          verify_gst_hz;
} gnss_nmea_config_t;

/* ---- API ---- */

void gnss_nmea_config_init(gnss_nmea_config_t* cfg,
                            uint8_t instance_id,
                            const char* name);

void gnss_nmea_config_set_sources(gnss_nmea_config_t* cfg,
                                   transport_uart_t*      uart,
                                   gnss_um980_t*          gnss);

void gnss_nmea_config_set_profile(gnss_nmea_config_t* cfg,
                                   const nmea_profile_t* profile);

void gnss_nmea_config_set_um980_port(gnss_nmea_config_t* cfg,
                                       const char* port);

void gnss_nmea_config_start_restore(gnss_nmea_config_t* cfg);

nmea_restore_state_t gnss_nmea_config_get_restore_state(
    const gnss_nmea_config_t* cfg);

const um980_cmd_result_t* gnss_nmea_config_get_last_result(
    const gnss_nmea_config_t* cfg);

uint32_t gnss_nmea_config_get_effect_flags(const gnss_nmea_config_t* cfg);

void gnss_nmea_config_service_step(runtime_component_t* comp,
                                   uint64_t timestamp_us);

/* ---- ACK Detection (public for unit testing) ---- */

bool um980_parse_response(const char* response, uint16_t resp_len,
                           um980_cmd_result_t* result);

#ifdef __cplusplus
}
#endif
