/* ========================================================================
 * gnss_um980_snapshot.h — UM980 Boot-Time Config Snapshot (NAV-UM980-CONFIG-SNAPSHOT-001)
 *
 * Queries both UM980 receivers at boot time using the gnss_um980_control layer.
 * Commands: UNLOGALL (optional), version, config, mode, mask
 *
 * Refactored for NAV-REMOTE-GNSS-CMD-001:
 *   - Uses gnss_um980_control for all UART communication
 *   - No direct transport_uart dependency
 *   - Boot sequence: settle → (UNLOGALL) → query → (restore NMEA) → continue
 *
 * Memory:
 *   - 16 KB per receiver (statically allocated)
 *   - ~33 KB total static RAM cost
 *
 * HTTP access (via remote_diag):
 *   - /gnss/config_snapshot  — combined boot-time snapshot
 *   - /gnss/1/config_snapshot — receiver 1 only
 *   - /gnss/2/config_snapshot — receiver 2 only
 *   - /gnss/config_status     — JSON status
 *
 * IMPORTANT: These show BOOT-TIME data. For live queries, use
 * /gnss/1/send/VERSIONA via the gnss_um980_control HTTP interface.
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

/* ---- Timing (ms) ---- */
#define GNSS_SNAPSHOT_SETTLE_MS       2000

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

/** Initialize snapshot buffers. Call once before run_all(). */
void gnss_um980_snapshot_init(void);

/**
 * Run config snapshot on both receivers using the gnss_um980_control layer.
 *
 * MUST be called AFTER gnss_um980_control_init() and register().
 * Sequence: settle → (UNLOGALL) → version/config/mode/mask → (restore NMEA)
 * Blocks for ~10s. On failure, boot continues.
 *
 * @return true if both receivers responded to all queries
 */
bool gnss_um980_snapshot_run_all(void);

/** Get raw ASCII snapshot for receiver 1. */
const char* gnss_um980_snapshot_get_receiver1(void);

/** Get raw ASCII snapshot for receiver 2. */
const char* gnss_um980_snapshot_get_receiver2(void);

/** Get combined snapshot (both receivers). */
const char* gnss_um980_snapshot_get_combined(void);

/** Check if snapshot has been completed. */
bool gnss_um980_snapshot_is_complete(void);

/** Get snapshot status (completion, timeouts, byte counts, duration). */
void gnss_um980_snapshot_get_status(gnss_um980_snapshot_status_t* out);

#ifdef __cplusplus
}
#endif
