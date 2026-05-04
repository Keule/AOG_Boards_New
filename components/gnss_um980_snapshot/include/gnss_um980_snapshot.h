#pragma once

/* ========================================================================
 * gnss_um980_snapshot.h — UM980 Boot-Time Config Snapshot (NAV-UM980-CONFIG-SNAPSHOT-001)
 *
 * Queries both UM980 receivers at boot time (version, config, mode, mask)
 * and stores raw ASCII responses in static buffers for web access.
 *
 * Timing:
 *   - Receiver settle delay: 2000 ms after UART init
 *   - Command spacing:       300 ms between commands
 *   - Response timeout:      1500 ms per command
 *   - Total receiver budget: ~10 s
 *
 * Memory:
 *   - 16 KB per receiver (statically allocated)
 *   - ~32 KB total static RAM cost
 * ======================================================================== */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Configuration ---- */
#define GNSS_SNAPSHOT_BUFFER_SIZE     16384   /* 16 KB per receiver */
#define GNSS_SNAPSHOT_RX_COUNT        2
#define GNSS_SNAPSHOT_CMDS            4       /* version, config, mode, mask */

/* ---- Timing (ms) ---- */
#define GNSS_SNAPSHOT_SETTLE_MS       2000
#define GNSS_SNAPSHOT_CMD_SPACING_MS  300
#define GNSS_SNAPSHOT_RESP_TIMEOUT_MS 1500

/* ---- UM980 read-only commands (forbidden: unlog, saveconfig, freset, mode changes) ---- */
static const char* const GNSS_SNAPSHOT_COMMANDS[GNSS_SNAPSHOT_CMDS] = {
    "version\r\n",
    "config\r\n",
    "mode\r\n",
    "mask\r\n"
};

/* ---- Status struct ---- */
typedef struct {
    bool rx1_complete;
    bool rx2_complete;
    bool rx1_timeout;
    bool rx2_timeout;
    size_t rx1_bytes;
    size_t rx2_bytes;
    uint32_t duration_ms;
} gnss_um980_snapshot_status_t;

/* ---- Public API ---- */

/**
 * Initialize snapshot buffers. Must be called once before run_all().
 * Can be called multiple times safely (no-op after first init).
 */
void gnss_um980_snapshot_init(void);

/**
 * Run config snapshot on both receivers.
 *
 * MUST be called AFTER UART transport is initialized but BEFORE
 * NTRIP connect, RTCM forwarding, and normal GNSS operation.
 *
 * Sequence:
 *   1. Wait for receiver settle (2s)
 *   2. Query receiver 1 (version, config, mode, mask)
 *   3. Query receiver 2 (version, config, mode, mask)
 *   4. Store raw ASCII responses
 *
 * This function blocks for ~10 seconds total (settle + 2 receivers).
 * On failure, system boot continues — does NOT abort.
 *
 * @param uart_primary   Pointer to primary transport_uart_t
 * @param uart_secondary Pointer to secondary transport_uart_t
 * @return true if both receivers responded, false if any timeout
 */
bool gnss_um980_snapshot_run_all(void* uart_primary, void* uart_secondary);

/**
 * Get raw ASCII snapshot for receiver 1.
 * @return Null-terminated string, or "NO SNAPSHOT DATA" if not available.
 */
const char* gnss_um980_snapshot_get_receiver1(void);

/**
 * Get raw ASCII snapshot for receiver 2.
 * @return Null-terminated string, or "NO SNAPSHOT DATA" if not available.
 */
const char* gnss_um980_snapshot_get_receiver2(void);

/**
 * Get combined snapshot (both receivers).
 * @return Null-terminated string with receiver 1 and 2 snapshots.
 */
const char* gnss_um980_snapshot_get_combined(void);

/**
 * Check if snapshot has been completed.
 */
bool gnss_um980_snapshot_is_complete(void);

/**
 * Get snapshot status (completion, timeouts, byte counts, duration).
 */
void gnss_um980_snapshot_get_status(gnss_um980_snapshot_status_t* out);

#ifdef __cplusplus
}
#endif
