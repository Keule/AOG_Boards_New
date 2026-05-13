#pragma once
/* ========================================================================
 * gnss_um980_control.h — Remote GNSS Command Interface (NAV-REMOTE-GNSS-CMD-001)
 *
 * SparkFun-style command/response layer for UM980 receivers.
 * Provides exclusive UART access with NMEA parser suspension,
 * automatic CRLF appending, and URL decoding.
 *
 * Safety:
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

/* ---- NMEA profile defaults (ADR-020 Task C) ---- *
 * Both RX1 and RX2 use COM2 (no port probing needed).
 * GGA 50 Hz, RMC 20 Hz, GSA 1 Hz epoch, GST 1 Hz, GSV disabled.
 */

/* ---- Error codes ---- */
typedef enum {
    GNSS_CTRL_OK = 0,              /* Command sent, response received */
    GNSS_CTRL_ERR_NOT_INITIALIZED, /* Control instance not initialized */
    GNSS_CTRL_ERR_INVALID_PARAM,   /* NULL pointer or invalid parameter */
    GNSS_CTRL_ERR_TIMEOUT,         /* No response within timeout */
    GNSS_CTRL_ERR_UART_BUSY,       /* Mutex acquire failed (command in progress) */
    GNSS_CTRL_ERR_TX_FAILED,       /* transport_uart_tx_write returned 0 */
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
 * URL-decode a string in place (%XX hex, + as space).
 * Decodes at most max_len bytes (including NUL terminator).
 * Returns length of decoded string.
 */
size_t gnss_um980_url_decode(char* str, size_t max_len);

/* ---- NMEA Profile Configuration (ADR-020 Task C) ---- *
 * Per-receiver NMEA sentence configuration.
 * Both RX1 and RX2 use COM2 (no port probing needed).
 */

/* Maximum candidate ports per receiver */
#define GNSS_NMEA_MAX_PORTS       2
#define GNSS_NMEA_MAX_PORT_LEN    8    /* e.g. "COM1", "COM2" */

/* GST classification states (ADR-020 Task E) */
typedef enum {
    GNSS_GST_DISABLED = 0,   /* GST is not enabled — gst_count should be 0 */
    GNSS_GST_OK,            /* GST enabled and in target rate range */
    GNSS_GST_UNEXPECTED,    /* GST running despite being disabled */
    GNSS_GST_MISSING        /* GST enabled but no GST sentences received */
} gnss_gst_class_t;

/* GSA classification states (ADR-020 Task E) */
typedef enum {
    GNSS_GSA_OK_SINGLE_TALKER = 0,  /* 1 Hz epoch, 1 sentence/s */
    GNSS_GSA_OK_MULTI_TALKER,       /* 1 Hz epoch, ~3 sentences/s (multi-constellation) */
    GNSS_GSA_RATE_MISMATCH,         /* Sentence rate outside expected range */
    GNSS_GSA_DISABLED                /* GSA not enabled */
} gnss_gsa_class_t;

/* NMEA sentence rate verification result (ADR-020 Task D) */
typedef struct {
    const char* sentence;       /* e.g. "GNGGA", "GNRMC" */
    float target_hz;            /* Target rate (e.g. 10.0) */
    float measured_hz;          /* Measured rate from counter delta */
    float tolerance_hz;         /* Acceptable deviation (e.g. 2.0) */
    bool  in_range;             /* measured within target +/- tolerance */
} gnss_rate_check_t;

/* Command ACK + Effect result (ADR-020 Task D) */
typedef struct {
    int    receiver;            /* 1 or 2 */
    const char* port;           /* e.g. "COM1", "COM2" */
    const char* command;        /* e.g. "GPGGA COM2 0.02" */
    int    ack_status;          /* 0=OK, >0 = gnss_ctrl_err_t code */
    bool   effect_verified;     /* true if post-command rate check passed */
    const char* effect_detail;  /* human-readable effect result */
    int    decision;            /* 0=ACCEPT, 1=REJECT_NO_ACK, 2=REJECT_NO_EFFECT */
} gnss_cmd_result_t;

/* Per-receiver NMEA profile (ADR-020 Task C) */
typedef struct {
    int    receiver_id;
    const char* candidate_ports[GNSS_NMEA_MAX_PORTS]; /* e.g. COM2 for both RX */
    int    candidate_count;
    const char* selected_port;    /* "COM2" for both receivers */
    float  gga_hz;               /* Target GGA rate (e.g. 50.0) */
    float  rmc_hz;               /* Target RMC rate (e.g. 20.0) */
    float  gsa_epoch_hz;         /* Target GSA epoch rate (e.g. 1.0) */
    float  gst_period;           /* GST period (1.0 = 1 Hz) */
    bool   gst_enabled;           /* Whether GST should be enabled */
    bool   gsv_enabled;           /* Whether GSV should be enabled */
    /* Post-apply verification results */
    bool   applied;               /* true if profile was applied */
    int    apply_result;          /* 0=SUCCESS, 1=PARTIAL, 2=FAILED */
    gnss_gst_class_t  gst_class;
    gnss_gsa_class_t  gsa_class;
    gnss_cmd_result_t cmd_results[8]; /* Results of individual LOG commands */
    int    cmd_result_count;
    /* Failure reason if apply_result != SUCCESS */
    char   failure_reason[128];
} gnss_nmea_profile_t;

/* Port probe result (ADR-020 Task C — legacy, both RX now use COM2) */
typedef struct {
    const char* port;
    int    probe_ack;           /* 0=ACK received, >0 = error code */
    bool   effect_gga_confirmed;
    bool   effect_rmc_confirmed;
    bool   effect_gsv_disabled;
    bool   overall_pass;
} gnss_port_probe_t;

/* Overall GNSS config state for a receiver */
typedef struct {
    int    receiver;
    const char* state_str;       /* "DONE", "FAILED", "PENDING" */
    const char* selected_port;
    gnss_gst_class_t  gst_class;
    gnss_gsa_class_t  gsa_class;
    gnss_port_probe_t port_probes[GNSS_NMEA_MAX_PORTS];
    int    port_probe_count;
    char   failure_reason[128];
} gnss_cfg_result_t;

/* Forward declarations for NMEA profile functions */
gnss_ctrl_err_t gnss_um980_apply_nmea_profile(gnss_um980_control_t* ctrl,
                                              gnss_nmea_profile_t* profile);
gnss_cfg_result_t gnss_um980_configure_rx2(gnss_um980_control_t* ctrl);
gnss_cfg_result_t gnss_um980_configure_rx1(gnss_um980_control_t* ctrl);

/* GST/GSA classification helpers */
gnss_gst_class_t gnss_classify_gst(bool gst_enabled, uint32_t gst_count, uint32_t elapsed_ms);
gnss_gsa_class_t gnss_classify_gsa(bool gsa_enabled, uint32_t gsa_count, uint32_t elapsed_ms,
                                   float gsa_epoch_hz);
const char* gnss_gst_class_str(gnss_gst_class_t c);
const char* gnss_gsa_class_str(gnss_gsa_class_t c);

#ifdef __cplusplus
}
#endif
