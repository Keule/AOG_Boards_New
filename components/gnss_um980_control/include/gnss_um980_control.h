#pragma once
/* ========================================================================
 * gnss_um980_control.h — Remote GNSS Command Interface (NAV-REMOTE-GNSS-CMD-001)
 *
 * SparkFun-style command/response layer for UM980 receivers.
 * Provides exclusive UART access with NMEA parser suspension,
 * automatic CRLF appending, URL decoding, and command blocklist.
 *
 * NAV-GNSS-NMEA-LOG-IDEMPOTENT-001 additions:
 *   - Idempotent NMEA restore: targeted UNLOG before LOG
 *   - Explicit GSV deactivation (all talker IDs)
 *   - Restore command result verification
 *   - Runtime rate guard with automatic recovery (max 1 per boot)
 *   - NMEA profile state tracking
 *
 * Safety:
 *   - Commands are validated against a blocklist (FRESET, SAVECONFIG, RESET, UPGRADE)
 *   - UART mutex ensures exclusive access during command cycles
 *   - NMEA parser is suspended (rx_source=NULL) to prevent data corruption
 *   - On timeout/failure: parser is always restored, system continues
 *
 * UART Ownership Model:
 *   1. Acquire mutex
 *   2. Suspend runtime parser (set rx_source = NULL)
 *   3. Flush RX ring buffer
 *   4. Send command via transport_uart_tx_write() + pump()
 *   5. Collect response via pump() + rx_read() with timeout/quiet detection
 *   6. Restore runtime parser
 *   7. Release mutex
 *
 * Usage:
 *   - Boot-time: gnss_um980_control_init() → gnss_um980_control_register()
 *   - Runtime:   HTTP handler calls gnss_um980_control_get(receiver) → send_command()
 *   - Snapshot:  gnss_um980_snapshot uses control registry for queries
 * ======================================================================== */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations — actual types use anonymous structs, so we use void*
 * in the public API and cast internally. This avoids incomplete-type errors
 * across component boundaries. */

/* ---- Configuration ---- */
#define GNSS_CTRL_MAX_RESPONSE      4096    /* Max response buffer size (bytes) */
#define GNSS_CTRL_DEFAULT_TIMEOUT   2000    /* Default command timeout (ms) */
#define GNSS_CTRL_QUIET_TIMEOUT    500     /* Quiet time to detect end of response (ms) */
#define GNSS_CTRL_MAX_RETRIES       2       /* Retries on timeout */
#define GNSS_CTRL_MUTEX_TIMEOUT    5000    /* Max wait for UART mutex (ms) */
#define GNSS_CTRL_CMD_MAX_LEN      256     /* Max command string length */

/* ---- Boot-time UNLOGALL flag ----
 * When enabled, the boot snapshot sends UNLOGALL before querying
 * and restores essential NMEA logs (GGA, RMC, GST, GSA) after.
 * GSV is explicitly deactivated. */
#define GNSS_CTRL_UNLOGALL_AT_BOOT  1

/* ---- NMEA Profile restore configuration ----
 * Rate guard monitors sentence rates after restore and triggers
 * automatic recovery if rates exceed thresholds. */
#define GNSS_CTRL_RATE_GUARD_WINDOW_MS    10000  /* Observe rates for 10s after restore */
#define GNSS_CTRL_RATE_GUARD_CHECK_MS     2000   /* Check rates every 2s during window */
#define GNSS_CTRL_MAX_RECOVERY_PER_BOOT   1      /* Max auto-recovery attempts per boot */

/* ---- NMEA rate thresholds (Hz * 100 for fixed-point) ---- */
#define GNSS_CTRL_RATE_GGA_THRESHOLD      3000   /* 30.00 Hz */
#define GNSS_CTRL_RATE_RMC_THRESHOLD      3000   /* 30.00 Hz */
#define GNSS_CTRL_RATE_GST_THRESHOLD      3000   /* 30.00 Hz */
#define GNSS_CTRL_RATE_GSA_THRESHOLD       500   /* 5.00 Hz */
#define GNSS_CTRL_RATE_GSV_THRESHOLD        20   /* 0.20 Hz — any GSV is suspicious */
#define GNSS_CTRL_RATE_TOTAL_THRESHOLD   9000    /* 90 sentences/s */

/* ---- NMEA profile state ---- */
typedef enum {
    NMEA_PROFILE_IDLE = 0,       /* No restore attempted yet */
    NMEA_PROFILE_RESTORING,      /* Restore sequence in progress */
    NMEA_PROFILE_ACTIVE,         /* Profile restored, rates normal */
    NMEA_PROFILE_HIGH_RATE,      /* Detected abnormally high NMEA rates */
    NMEA_PROFILE_GSV_ACTIVE,     /* GSV sentences detected (should be off) */
    NMEA_PROFILE_DUPLICATE,      /* Duplicate log suspected (very high rates) */
    NMEA_PROFILE_RECOVERY_DONE,  /* Recovery attempted (success or failure) */
    NMEA_PROFILE_RECOVERY_FAILED,/* Recovery failed, manual intervention needed */
} nmea_profile_state_t;

/* ---- Error codes ---- */
typedef enum {
    GNSS_CTRL_OK = 0,              /* Command sent, response received */
    GNSS_CTRL_ERR_NOT_INITIALIZED, /* Control instance not initialized */
    GNSS_CTRL_ERR_INVALID_PARAM,   /* NULL pointer or invalid parameter */
    GNSS_CTRL_ERR_TIMEOUT,         /* No response within timeout */
    GNSS_CTRL_ERR_UART_BUSY,       /* Mutex acquire failed (command in progress) */
    GNSS_CTRL_ERR_TX_FAILED,       /* transport_uart_tx_write returned 0 */
    GNSS_CTRL_ERR_BLOCKED,         /* Command matched blocklist prefix */
    GNSS_CTRL_ERR_EXPECT_FAILED,   /* Expected header not found in response */
    GNSS_CTRL_ERR_RESTORE_FAILED,  /* One or more restore commands failed */
} gnss_ctrl_err_t;

/* ---- Per-command restore result ---- */
typedef struct {
    const char* cmd;          /* Command string (points to static data) */
    gnss_ctrl_err_t err;      /* Result of send_command() */
    uint32_t response_len;    /* Length of response received */
    bool response_has_ok;     /* Response contained "OK" or "<OK" */
} gnss_ctrl_restore_result_t;

/* ---- Control instance ---- */
typedef struct {
    /* Set by init (NOT owned) — stored as void* to avoid anonymous struct issues */
    void*               uart;      /* transport_uart_t* — Transport UART for TX/RX */
    void*               gnss;      /* gnss_um980_t* — NMEA parser (may be NULL for boot-only) */
    int                 receiver;  /* 1 or 2 */

    /* Internal state */
    void*            mutex;            /* SemaphoreHandle_t */
    volatile bool    initialized;
    volatile bool    command_active;

    /* Statistics */
    uint32_t commands_sent;
    uint32_t commands_ok;
    uint32_t commands_timeout;
    uint32_t commands_blocked;

    /* ---- NMEA Profile State (NAV-GNSS-NMEA-LOG-IDEMPOTENT-001) ---- */
    nmea_profile_state_t profile_state;
    uint32_t profile_restore_count;        /* How many full restore cycles attempted */
    uint32_t profile_recovery_count;       /* How many auto-recoveries attempted */
    uint32_t profile_last_restore_ms;      /* Timestamp of last restore */
    uint32_t profile_last_recovery_ms;     /* Timestamp of last auto-recovery */
    uint32_t profile_rate_guard_start_ms;  /* When rate guard window started */
    bool     profile_rate_guard_active;    /* Rate guard is monitoring */
    gnss_ctrl_err_t profile_last_error;    /* Last restore error code */

    /* Last restore results (for diagnostics) */
    #define GNSS_CTRL_RESTORE_RESULTS_MAX 20
    gnss_ctrl_restore_result_t restore_results[GNSS_CTRL_RESTORE_RESULTS_MAX];
    uint8_t restore_result_count;
} gnss_um980_control_t;

/* ---- Public API ---- */

/**
 * Initialize a control instance for one receiver.
 * Must be called before any command operations.
 *
 * @param ctrl     Control instance (caller-allocated)
 * @param receiver Receiver number (1 = primary, 2 = secondary)
 * @param uart     Transport UART instance (NOT owned)
 * @param gnss     GNSS parser instance (NOT owned, may be NULL)
 */
void gnss_um980_control_init(gnss_um980_control_t* ctrl, int receiver,
                             void* uart,
                             void* gnss);

/**
 * Register a control instance in the global registry.
 * HTTP handlers access control instances via gnss_um980_control_get().
 */
void gnss_um980_control_register(gnss_um980_control_t* ctrl);

/**
 * Get a registered control instance by receiver number.
 * Returns NULL if not registered.
 */
gnss_um980_control_t* gnss_um980_control_get(int receiver);

/**
 * Send a command and collect the response.
 *
 * Automatically:
 *   - Validates against blocklist
 *   - Appends \r\n if not present
 *   - Suspends NMEA parser during command
 *   - Retries on timeout (up to GNSS_CTRL_MAX_RETRIES)
 *
 * @param ctrl          Control instance
 * @param cmd           Command string (e.g. "VERSIONA", "CONFIG")
 * @param response      Output buffer for raw ASCII response
 * @param response_size Size of response buffer
 * @param timeout_ms    Per-attempt timeout (0 = GNSS_CTRL_DEFAULT_TIMEOUT)
 * @return GNSS_CTRL_OK on success, error code otherwise
 */
gnss_ctrl_err_t gnss_um980_send_command(gnss_um980_control_t* ctrl,
                                        const char* cmd,
                                        char* response, size_t response_size,
                                        uint32_t timeout_ms);

/**
 * Send a command and verify the response starts with expected_header.
 * Response is still filled with raw data.
 *
 * @param ctrl            Control instance
 * @param cmd             Command string
 * @param expected_header Expected start of response (e.g. "VERSIONA", "CONFIG")
 * @param response        Output buffer
 * @param response_size   Size of response buffer
 * @param timeout_ms      Per-attempt timeout
 * @return GNSS_CTRL_OK if response starts with header, GNSS_CTRL_ERR_EXPECT_FAILED otherwise
 */
gnss_ctrl_err_t gnss_um980_send_command_expect(gnss_um980_control_t* ctrl,
                                               const char* cmd,
                                               const char* expected_header,
                                               char* response, size_t response_size,
                                               uint32_t timeout_ms);

/**
 * Check if a command string is blocked by the safety blocklist.
 * Blocked prefixes: FRESET, SAVECONFIG, RESET, UPGRADE (case-insensitive).
 */
bool gnss_um980_control_is_command_blocked(const char* cmd);

/**
 * Send UNLOGALL to stop all NMEA logging.
 * Typically called before querying config to get clean responses.
 */
gnss_ctrl_err_t gnss_um980_control_unlogall(gnss_um980_control_t* ctrl);

/**
 * Idempotent NMEA profile restore (NAV-GNSS-NMEA-LOG-IDEMPOTENT-001).
 *
 * Phase 1 — Deactivate all logs on COM2:
 *   UNLOG COM2 GNGGA, GPGGA, GNRMC, GPRMC, GNGST, GPGST,
 *           GNGSA, GPGSA, GNGSV, GPGSV, GBGSV, GAGSV
 *
 * Phase 2 — Activate runtime profile (exactly once):
 *   LOG COM2 GNGGA ONTIME 0.05  (20 Hz)
 *   LOG COM2 GNRMC ONTIME 0.05  (20 Hz)
 *   LOG COM2 GNGST ONTIME 0.05  (20 Hz)
 *   LOG COM2 GNGSA ONTIME 1     (1 Hz)
 *
 * All commands use LOG COM2 <SENTENCE> ONTIME <PERIOD> syntax.
 * No short-form commands (e.g. "GNGGA COM2 0.05") are used.
 *
 * Each command result is stored in ctrl->restore_results[].
 * If any command fails, profile_last_error is set and the function
 * returns GNSS_CTRL_ERR_RESTORE_FAILED, but all commands are still
 * attempted (best-effort).
 *
 * @return GNSS_CTRL_OK if all commands succeeded,
 *         GNSS_CTRL_ERR_RESTORE_FAILED if any command failed.
 */
gnss_ctrl_err_t gnss_um980_control_restore_nmea(gnss_um980_control_t* ctrl);

/**
 * URL-decode a string in place (%XX hex, + as space).
 * Decodes at most max_len bytes (including NUL terminator).
 * Returns length of decoded string.
 */
size_t gnss_um980_url_decode(char* str, size_t max_len);

#ifdef __cplusplus
}
#endif
