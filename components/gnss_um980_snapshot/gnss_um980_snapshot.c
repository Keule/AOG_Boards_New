/* ========================================================================
 * gnss_um980_snapshot.c — UM980 Boot-Time Config Snapshot (NAV-UM980-CONFIG-SNAPSHOT-001)
 *
 * Refactored for NAV-REMOTE-GNSS-CMD-001:
 *   - Uses gnss_um980_control layer for all UART communication
 *   - No direct transport_uart dependency (type-erased forward declarations removed)
 *   - Control layer handles parser suspension, UART exclusive access, retries
 *
 * Boot sequence:
 *   1. Wait for receiver settle (2s)
 *   2. Optionally send UNLOGALL (GNSS_CTRL_UNLOGALL_AT_BOOT)
 *   3. Query version, config, mode, mask for each receiver
 *   4. Optionally restore essential NMEA logs
 *   5. Store raw ASCII responses in static buffers
 *
 * Safety:
 *   - NO saveconfig, freset, or mode-changing commands
 *   - Failure does NOT block system boot
 *   - UNLOGALL disabled by default (compile-time flag)
 * ======================================================================== */

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gnss_um980_snapshot.h"
#include "gnss_um980_control.h"

static const char* TAG = "SNAPSHOT";

/* ---- Static buffers ---- */
static char s_snapshot[GNSS_SNAPSHOT_RX_COUNT][GNSS_SNAPSHOT_BUFFER_SIZE];

/* ---- Combined buffer (lazily built) ---- */
static char s_combined[GNSS_SNAPSHOT_BUFFER_SIZE * 2 + 256];

/* ---- Write positions ---- */
static size_t s_write_pos[GNSS_SNAPSHOT_RX_COUNT];

/* ---- Status ---- */
static volatile bool s_initialized = false;
static volatile bool s_complete = false;
static bool s_rx_complete[GNSS_SNAPSHOT_RX_COUNT];
static bool s_rx_timeout[GNSS_SNAPSHOT_RX_COUNT];
static size_t s_rx_bytes[GNSS_SNAPSHOT_RX_COUNT];
static uint32_t s_duration_ms = 0;

/* ---- NAV-GNSS-NMEA-LOG-IDEMPOTENT-001: Per-receiver error tracking ---- */
static uint8_t s_rx_blocked_count[GNSS_SNAPSHOT_RX_COUNT];
static uint8_t s_rx_error_count[GNSS_SNAPSHOT_RX_COUNT];
static uint8_t s_rx_timeout_count[GNSS_SNAPSHOT_RX_COUNT];
static bool s_required_ok = false;

/* ---- Timing macro ---- */
#define SNAPSHOT_DELAY_MS(ms) vTaskDelay(pdMS_TO_TICKS(ms))

/* ---- Query commands ---- */
static const char* const SNAPSHOT_CMDS[] = {
    "VERSIONA",
    "CONFIG",
    "MODE",
    "MASK",
};
#define SNAPSHOT_CMD_COUNT (sizeof(SNAPSHOT_CMDS) / sizeof(SNAPSHOT_CMDS[0]))

/* =================================================================
 * Internal: append bytes to receiver snapshot buffer
 * ================================================================= */
static void snapshot_append(int rx_index, const char* data, size_t len)
{
    if (rx_index < 0 || rx_index >= GNSS_SNAPSHOT_RX_COUNT) return;
    if (data == NULL || len == 0) return;

    size_t avail = GNSS_SNAPSHOT_BUFFER_SIZE - 1;
    size_t pos = s_write_pos[rx_index];

    if (pos >= avail) return;

    size_t to_copy = len;
    if (to_copy > avail - pos) to_copy = avail - pos;

    memcpy(s_snapshot[rx_index] + pos, data, to_copy);
    s_write_pos[rx_index] = pos + to_copy;
    s_snapshot[rx_index][s_write_pos[rx_index]] = '\0';
    s_rx_bytes[rx_index] = s_write_pos[rx_index];
}

/* =================================================================
 * Internal: append formatted line
 * ================================================================= */
static void snapshot_append_cmd(int rx_index, const char* cmd, const char* status)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "> %s  %s\r\n", cmd, status);
    if (n > 0 && (size_t)n < sizeof(buf)) {
        snapshot_append(rx_index, buf, (size_t)n);
    }
}

/* =================================================================
 * Internal: append separator line
 * ================================================================= */
static void snapshot_append_separator(int rx_index, const char* label)
{
    char sep[128];
    int n = snprintf(sep, sizeof(sep), "\r\n===== %s =====\r\n", label);
    if (n > 0 && (size_t)n < sizeof(sep)) {
        snapshot_append(rx_index, sep, (size_t)n);
    }
}

/* =================================================================
 * Internal: query one receiver using control layer
 * ================================================================= */
static bool snapshot_query_receiver(int rx_index)
{
    int receiver = rx_index + 1;
    gnss_um980_control_t* ctrl = gnss_um980_control_get(receiver);

    if (ctrl == NULL) {
        ESP_LOGE(TAG, "Receiver %d: control instance not registered", receiver);
        return false;
    }

    char header[80];
    snprintf(header, sizeof(header), "UM980 RECEIVER %d SNAPSHOT", receiver);
    snapshot_append_separator(rx_index, header);

    bool all_ok = true;
    char response[GNSS_CTRL_MAX_RESPONSE];

    for (size_t cmd_idx = 0; cmd_idx < SNAPSHOT_CMD_COUNT; cmd_idx++) {
        const char* cmd = SNAPSHOT_CMDS[cmd_idx];

        gnss_ctrl_err_t err = gnss_um980_send_command(ctrl, cmd,
                                                       response, sizeof(response),
                                                       GNSS_CTRL_DEFAULT_TIMEOUT);

        if (err == GNSS_CTRL_OK) {
            snapshot_append_cmd(rx_index, cmd, "[OK]");
            if (response[0] != '\0') {
                snapshot_append(rx_index, response, strlen(response));
            }
        } else if (err == GNSS_CTRL_ERR_BLOCKED) {
            /* NAV-GNSS-NMEA-LOG-IDEMPOTENT-001:
             * Blocked commands (SAVECONFIG, RESET, UPGRADE, FRESET) are
             * intentionally blocked. They are NOT required for snapshot
             * success. Count separately, do NOT set failure. */
            s_rx_blocked_count[rx_index]++;
            ESP_LOGI(TAG, "SNAPSHOT: RX%d [%s] BLOCKED_BY_POLICY (skipped, count=%d)",
                     receiver, cmd, (int)s_rx_blocked_count[rx_index]);
            snapshot_append_cmd(rx_index, cmd, "[BLOCKED_BY_POLICY]");
        } else {
            /* TIMEOUT or other error — this IS a failure */
            s_rx_error_count[rx_index]++;
            const char* label = (err == GNSS_CTRL_ERR_TIMEOUT) ? "[TIMEOUT]" : "[ERROR]";
            if (err == GNSS_CTRL_ERR_TIMEOUT) {
                s_rx_timeout_count[rx_index]++;
                s_rx_timeout[rx_index] = true;
            }
            ESP_LOGE(TAG, "SNAPSHOT: RX%d [%s] %s (error_count=%d)",
                     receiver, cmd, label, (int)s_rx_error_count[rx_index]);
            snapshot_append_cmd(rx_index, cmd, label);
            all_ok = false;
        }

        /* Separator between commands */
        snapshot_append(rx_index, "\r\n", 2);
    }

    /* End marker */
    snapshot_append(rx_index, "===== END =====\r\n", 17);

    s_rx_complete[rx_index] = all_ok;
    return all_ok;
}

/* =================================================================
 * Internal: build combined snapshot string
 * ================================================================= */
static void build_combined(void)
{
    size_t pos = 0;
    size_t cap = sizeof(s_combined) - 1;

    if (s_write_pos[0] > 0) {
        size_t len = s_write_pos[0];
        if (len > cap) len = cap;
        memcpy(s_combined + pos, s_snapshot[0], len);
        pos += len;
    }

    if (s_write_pos[0] > 0 && s_write_pos[1] > 0) {
        const char* sep = "\r\n\r\n========================================\r\n\r\n";
        size_t sep_len = strlen(sep);
        if (pos + sep_len < cap) {
            memcpy(s_combined + pos, sep, sep_len);
            pos += sep_len;
        }
    }

    if (s_write_pos[1] > 0) {
        size_t len = s_write_pos[1];
        if (pos + len > cap) len = cap - pos;
        memcpy(s_combined + pos, s_snapshot[1], len);
        pos += len;
    }

    s_combined[pos] = '\0';
}

/* =================================================================
 * Public API
 * ================================================================= */

void gnss_um980_snapshot_init(void)
{
    if (s_initialized) return;

    memset(s_snapshot, 0, sizeof(s_snapshot));
    memset(s_combined, 0, sizeof(s_combined));
    memset(s_write_pos, 0, sizeof(s_write_pos));
    memset(s_rx_complete, 0, sizeof(s_rx_complete));
    memset(s_rx_timeout, 0, sizeof(s_rx_timeout));
    memset(s_rx_bytes, 0, sizeof(s_rx_bytes));
    memset(s_rx_blocked_count, 0, sizeof(s_rx_blocked_count));
    memset(s_rx_error_count, 0, sizeof(s_rx_error_count));
    memset(s_rx_timeout_count, 0, sizeof(s_rx_timeout_count));
    s_complete = false;
    s_duration_ms = 0;
    s_required_ok = false;

    s_initialized = true;
}

bool gnss_um980_snapshot_run_all(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "gnss_um980_snapshot_run_all called before init");
        return false;
    }

    /* Verify control layer is available */
    if (gnss_um980_control_get(1) == NULL || gnss_um980_control_get(2) == NULL) {
        ESP_LOGE(TAG, "Control layer not registered — cannot run snapshot");
        s_complete = true;
        return false;
    }

    uint32_t start = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "UM980 Config Snapshot starting — receiver settle delay %d ms",
             GNSS_SNAPSHOT_SETTLE_MS);

    /* Wait for receivers to boot and stabilize */
    SNAPSHOT_DELAY_MS(GNSS_SNAPSHOT_SETTLE_MS);

#if GNSS_CTRL_UNLOGALL_AT_BOOT
    /* Optional: stop all NMEA logging for clean query responses */
    ESP_LOGI(TAG, "Sending UNLOGALL to both receivers...");
    gnss_um980_control_unlogall(gnss_um980_control_get(1));
    gnss_um980_control_unlogall(gnss_um980_control_get(2));
    SNAPSHOT_DELAY_MS(500);
#endif

    /* Query receiver 1 */
    ESP_LOGI(TAG, "Querying UM980 Receiver 1...");
    bool rx1_ok = snapshot_query_receiver(0);
    ESP_LOGI(TAG, "Receiver 1: %s (%zu bytes)",
             rx1_ok ? "OK" : "TIMEOUT/ERROR", s_rx_bytes[0]);

    /* Query receiver 2 */
    ESP_LOGI(TAG, "Querying UM980 Receiver 2...");
    bool rx2_ok = snapshot_query_receiver(1);
    ESP_LOGI(TAG, "Receiver 2: %s (%zu bytes)",
             rx2_ok ? "OK" : "TIMEOUT/ERROR", s_rx_bytes[1]);

#if GNSS_CTRL_UNLOGALL_AT_BOOT
    /* Restore essential NMEA logs — idempotent (NAV-GNSS-NMEA-LOG-IDEMPOTENT-001) */
    ESP_LOGI(TAG, "Restoring NMEA logs on both receivers (idempotent)...");
    gnss_ctrl_err_t r1_err = gnss_um980_control_restore_nmea(gnss_um980_control_get(1));
    gnss_ctrl_err_t r2_err = gnss_um980_control_restore_nmea(gnss_um980_control_get(2));
    if (r1_err != GNSS_CTRL_OK) {
        ESP_LOGW(TAG, "RX1 NMEA restore returned err=%d (non-fatal, rate guard active)", (int)r1_err);
    }
    if (r2_err != GNSS_CTRL_OK) {
        ESP_LOGW(TAG, "RX2 NMEA restore returned err=%d (non-fatal, rate guard active)", (int)r2_err);
    }
#endif

    s_duration_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - start;
    s_complete = true;

    /* NAV-GNSS-NMEA-LOG-IDEMPOTENT-001:
     * Snapshot is OK if all required read-only commands succeeded.
     * Blocked-by-policy commands (SAVECONFIG/RESET) do NOT count as failure. */
    s_required_ok = rx1_ok && rx2_ok;

    build_combined();

    ESP_LOGI(TAG, "Config Snapshot complete in %lu ms — RX1: %s (%zu B, blocked=%d, errors=%d), "
             "RX2: %s (%zu B, blocked=%d, errors=%d) required_ok=%s",
             (unsigned long)s_duration_ms,
             rx1_ok ? "OK" : "FAIL", s_rx_bytes[0],
             (int)s_rx_blocked_count[0], (int)s_rx_error_count[0],
             rx2_ok ? "OK" : "FAIL", s_rx_bytes[1],
             (int)s_rx_blocked_count[1], (int)s_rx_error_count[1],
             s_required_ok ? "true" : "false");

    return s_required_ok;
}

const char* gnss_um980_snapshot_get_receiver1(void)
{
    if (!s_initialized || s_write_pos[0] == 0) return "NO SNAPSHOT DATA";
    return s_snapshot[0];
}

const char* gnss_um980_snapshot_get_receiver2(void)
{
    if (!s_initialized || s_write_pos[1] == 0) return "NO SNAPSHOT DATA";
    return s_snapshot[1];
}

const char* gnss_um980_snapshot_get_combined(void)
{
    if (!s_initialized) return "NO SNAPSHOT DATA";
    return s_combined;
}

bool gnss_um980_snapshot_is_complete(void)
{
    return s_complete;
}

void gnss_um980_snapshot_get_status(gnss_um980_snapshot_status_t* out)
{
    if (out == NULL) return;
    out->rx1_complete = s_rx_complete[0];
    out->rx2_complete = s_rx_complete[1];
    out->rx1_timeout = s_rx_timeout[0];
    out->rx2_timeout = s_rx_timeout[1];
    out->rx1_bytes = s_rx_bytes[0];
    out->rx2_bytes = s_rx_bytes[1];
    out->duration_ms = s_duration_ms;
    /* NAV-GNSS-NMEA-LOG-IDEMPOTENT-001 */
    out->rx1_blocked_count = s_rx_blocked_count[0];
    out->rx2_blocked_count = s_rx_blocked_count[1];
    out->rx1_error_count = s_rx_error_count[0];
    out->rx2_error_count = s_rx_error_count[1];
    out->rx1_timeout_count = s_rx_timeout_count[0];
    out->rx2_timeout_count = s_rx_timeout_count[1];
    out->required_ok = s_required_ok;
}
