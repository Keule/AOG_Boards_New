#pragma once
/* ========================================================================
 * gnss_um980_control.h — Remote GNSS Command Interface (NAV-REMOTE-GNSS-CMD-001)
 *
 * SparkFun-style command/response layer for UM980 receivers.
 * Provides exclusive UART access with NMEA parser suspension,
 * automatic CRLF appending, URL decoding, and command blocklist.
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
 * and restores essential NMEA logs (GGA, RMC, GST) after.
 * DISABLED BY DEFAULT — enable only after verifying UM980 LOG command syntax. */
#define GNSS_CTRL_UNLOGALL_AT_BOOT  1

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
} gnss_ctrl_err_t;

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
 * Restore essential NMEA logs after UNLOGALL.
 * Sends LOG commands for GNGGA, GNRMC, GNGST, GNGSV, GNGSA on COM2
 * at appropriate rates for 20 Hz operation.
 */
void gnss_um980_control_restore_nmea(gnss_um980_control_t* ctrl);

/**
 * URL-decode a string in place (%XX hex, + as space).
 * Decodes at most max_len bytes (including NUL terminator).
 * Returns length of decoded string.
 */
size_t gnss_um980_url_decode(char* str, size_t max_len);

#ifdef __cplusplus
}
#endif
