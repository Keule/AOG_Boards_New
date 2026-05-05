#pragma once
/* ========================================================================
 * gnss_nmea_config.h — UM980 NMEA Configuration Manager
 *                        (NAV-UM980-LOG-SYNTAX-RATE-FIX-001)
 *
 * Sends LOG/UNLOG commands to UM980 receivers via UART TX.
 * Reads responses from UART RX (intercepted during restore phase).
 * Provides idempotent NMEA restore: UNLOG all → LOG target profile.
 *
 * Architecture:
 *   - During restore, this component intercepts the UART RX buffer
 *     before gnss_um980 reads it. NMEA data is forwarded via
 *     gnss_um980_feed() so parsing continues uninterrupted.
 *   - After restore, the component goes IDLE and gnss_um980 reads
 *     from the UART RX buffer directly again.
 *   - Runs on Core 0 via SERVICE_GROUP_UART.
 *
 * HARD RULES:
 *   - No network code
 *   - No blocking UART reads
 *   - No steering changes
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

/* ---- UM980 Command/Response Constants ---- */

/* Max command string length (including CRLF) */
#define UM980_CMD_MAX_LEN        80

/* Max response capture length */
#define UM980_RESP_MAX_LEN       512

/* Max number of commands in restore queue */
#define UM980_CMD_QUEUE_MAX      16

/* Response timeout: 500ms */
#define UM980_RESP_TIMEOUT_MS    500

/* Inter-command delay: 200ms */
#define UM980_INTER_CMD_DELAY_MS 200

/* ---- Command Result Status ---- */

typedef enum {
    UM980_CMD_PENDING = 0,     /* Command not yet sent */
    UM980_CMD_SENT,            /* Command sent, waiting for response */
    UM980_CMD_OK,              /* Response contains "OK" */
    UM980_CMD_ERROR,           /* Response contains "ERROR" */
    UM980_CMD_TIMEOUT,         /* No response within timeout */
    UM980_CMD_TRANSPORT_FAIL,  /* Could not write to TX buffer */
    UM980_CMD_EFFECT_NO_ACK    /* No OK/ERROR but effect observed */
} um980_cmd_status_t;

/* ---- Restore State ---- */

typedef enum {
    NMEA_RESTORE_IDLE = 0,     /* No restore in progress */
    NMEA_RESTORE_UNLOGGING,    /* Sending UNLOG commands */
    NMEA_RESTORE_SETTING,      /* Sending LOG commands */
    NMEA_RESTORE_DONE,         /* All commands completed */
    NMEA_RESTORE_FAILED        /* At least one critical command failed */
} nmea_restore_state_t;

/* ---- Command Result ---- */

typedef struct {
    um980_cmd_status_t status;
    bool    transport_ok;       /* bytes written to TX buffer */
    bool    response_received;  /* any data received from UM980 */
    bool    ack_ok;             /* "OK" found in response */
    bool    ack_error;          /* "ERROR" found in response */
    char    response_excerpt[128]; /* first 127 chars of response */
    uint16_t response_len;     /* total response length */
} um980_cmd_result_t;

/* ---- Restore Profile Definition ---- */

/* Target NMEA rates */
#define UM980_TARGET_GGA_PERIOD_S    0.05f   /* 20 Hz */
#define UM980_TARGET_RMC_PERIOD_S    0.05f   /* 20 Hz */
#define UM980_TARGET_GST_PERIOD_S    0.05f   /* 20 Hz */
#define UM980_TARGET_GSA_PERIOD_S    1.0f    /*  1 Hz */

/* UM980 port identifier */
#define UM980_PORT_COM2  "COM2"

/* ---- NMEA Config Instance ---- */

typedef struct gnss_nmea_config_t {
    runtime_component_t component;    /* MUST be first */

    /* Identity */
    uint8_t     instance_id;     /* 0 = primary, 1 = secondary */
    const char* name;

    /* ---- External references (set by app_core, NOT owned) ---- */
    transport_uart_t*  uart;    /* UART transport for TX/RX */
    gnss_um980_t*      gnss;    /* GNSS receiver for passthrough feed */

    /* ---- Restore state machine ---- */
    nmea_restore_state_t restore_state;
    uint8_t  cmd_index;            /* current command in queue */
    uint8_t  cmd_count;            /* total commands in queue */
    char     cmd_queue[UM980_CMD_QUEUE_MAX][UM980_CMD_MAX_LEN];
    uint64_t cmd_sent_us;          /* timestamp when last command was sent */
    uint64_t last_cmd_us;          /* timestamp of last command completion */
    uint64_t restore_start_us;     /* timestamp when restore started */

    /* ---- Response capture ---- */
    char     response_buf[UM980_RESP_MAX_LEN];
    uint16_t response_pos;

    /* ---- Last command result ---- */
    um980_cmd_result_t last_result;

    /* ---- Overall restore results ---- */
    uint8_t restore_ok_count;
    uint8_t restore_fail_count;
    char    restore_fail_cmd[UM980_CMD_MAX_LEN]; /* first failed command */

    /* ---- UART overrun diagnosis ---- */
    bool     uart_overrun_due_to_rate;
    uint32_t uart_overruns_at_last_rate_check;
} gnss_nmea_config_t;

/* ---- API ---- */

/* Initialize NMEA config manager.
 * Sets defaults, registers service_step callback. */
void gnss_nmea_config_init(gnss_nmea_config_t* cfg, uint8_t instance_id, const char* name);

/* Set external references (called by app_core during wiring).
 * uart: transport_uart_t for TX command sending and RX response reading.
 * gnss: gnss_um980_t for NMEA data passthrough during restore. */
void gnss_nmea_config_set_sources(gnss_nmea_config_t* cfg,
                                   transport_uart_t* uart,
                                   gnss_um980_t* gnss);

/* Start NMEA restore sequence.
 * Builds UNLOG + LOG command queue and begins execution.
 * Must be called after UART and GNSS are initialized. */
void gnss_nmea_config_start_restore(gnss_nmea_config_t* cfg);

/* Get current restore state. */
nmea_restore_state_t gnss_nmea_config_get_restore_state(const gnss_nmea_config_t* cfg);

/* Get pointer to last command result. */
const um980_cmd_result_t* gnss_nmea_config_get_last_result(const gnss_nmea_config_t* cfg);

/* Service step: execute restore state machine.
 * During restore, intercepts UART RX buffer and forwards NMEA data to gnss.
 * Called by the runtime service loop (Core 0, SERVICE_GROUP_UART). */
void gnss_nmea_config_service_step(runtime_component_t* comp, uint64_t timestamp_us);

/* ---- ACK Detection (public for unit testing) ---- */

/* Parse UM980 response and detect OK/ERROR.
 * response: raw response bytes from UM980.
 * resp_len: number of bytes in response.
 * result: output — filled with detected status.
 * Returns true if a definitive status was found (OK or ERROR). */
bool um980_parse_response(const char* response, uint16_t resp_len,
                           um980_cmd_result_t* result);

#ifdef __cplusplus
}
#endif
