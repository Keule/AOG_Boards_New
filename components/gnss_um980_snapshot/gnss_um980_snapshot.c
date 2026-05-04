/* ========================================================================
 * gnss_um980_snapshot.c — UM980 Boot-Time Config Snapshot (NAV-UM980-CONFIG-SNAPSHOT-001)
 *
 * Queries both UM980 receivers at boot time with read-only commands:
 *   version, config, mode, mask
 *
 * Implementation:
 *   - Sends commands via transport_uart_tx_write()
 *   - Reads responses via transport_uart_rx_read() + transport_uart_rx_available()
 *   - Stores raw ASCII in static 16KB buffers (one per receiver)
 *   - Blocks during snapshot (~10s total) — runs BEFORE runtime loop starts
 *
 * Safety:
 *   - NO unlog, saveconfig, freset, or mode-changing commands
 *   - NO RTCM forwarding during snapshot
 *   - NO NTRIP connect during snapshot
 *   - On timeout/failure: system boot continues normally
 * ======================================================================== */

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gnss_um980_snapshot.h"

/* ---- Forward declarations from transport_uart ---- */
/* We use a minimal type-erased API to avoid header dependency cycles */
typedef struct {
    /* Only the fields we need — verified against transport_uart_t */
    void*  _opaque_component;           /* runtime_component_t — offset 0 */
    int    _port;                       /* board_uart_port_t */
    uint32_t _baudrate;
} transport_uart_peek_t;

/* ---- Static buffers ---- */
static char s_snapshot[GNSS_SNAPSHOT_RX_COUNT][GNSS_SNAPSHOT_BUFFER_SIZE];

/* ---- Combined buffer ( lazily built ) ---- */
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

/* ---- Timing macros ---- */
#define SNAPSHOT_DELAY_MS(ms) vTaskDelay(pdMS_TO_TICKS(ms))
#define SNAPSHOT_TICK_MS()    ((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS))

/* =================================================================
 * Internal: get current time in ms
 * ================================================================= */
static uint32_t snapshot_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* =================================================================
 * Internal: append bytes to receiver snapshot buffer
 * ================================================================= */
static void snapshot_append(int rx_index, const char* data, size_t len)
{
    if (rx_index < 0 || rx_index >= GNSS_SNAPSHOT_RX_COUNT) return;
    if (data == NULL || len == 0) return;

    size_t avail = GNSS_SNAPSHOT_BUFFER_SIZE - 1;  /* leave room for NUL */
    size_t pos = s_write_pos[rx_index];

    if (pos >= avail) return;  /* buffer full */

    size_t to_copy = len;
    if (to_copy > avail - pos) {
        to_copy = avail - pos;
    }

    memcpy(s_snapshot[rx_index] + pos, data, to_copy);
    s_write_pos[rx_index] = pos + to_copy;
    s_snapshot[rx_index][s_write_pos[rx_index]] = '\0';
    s_rx_bytes[rx_index] = s_write_pos[rx_index];
}

/* =================================================================
 * Internal: append formatted line ("> command\r\n")
 * ================================================================= */
static void snapshot_append_cmd(int rx_index, const char* cmd)
{
    /* Strip \r\n from display command */
    char display[64];
    const char* p = cmd;
    while (*p && (*p == '\r' || *p == '\n')) p++;

    int n = snprintf(display, sizeof(display), "> %s", p);
    if (n > 0 && (size_t)n < sizeof(display)) {
        snapshot_append(rx_index, display, (size_t)n);
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

/* ---- Forward declarations from transport_uart (type-erased) ---- */
extern size_t transport_uart_rx_available(void* uart);
extern size_t transport_uart_rx_read(void* uart, uint8_t* buf, size_t max);
extern size_t transport_uart_tx_write(void* uart, const uint8_t* data, size_t len);

/* =================================================================
 * Internal: query one receiver (blocking, ~5s)
 *
 * Sends version, config, mode, mask commands sequentially.
 * Reads all available response data between commands.
 * ================================================================= */
static bool snapshot_query_receiver(int rx_index, void* uart)
{
    if (uart == NULL) return false;

    int rx_num = rx_index + 1;
    char header[80];
    snprintf(header, sizeof(header), "UM980 RECEIVER %d SNAPSHOT", rx_num);
    snapshot_append_separator(rx_index, header);

    bool all_ok = true;

    for (int cmd_idx = 0; cmd_idx < GNSS_SNAPSHOT_CMDS; cmd_idx++) {
        const char* cmd = GNSS_SNAPSHOT_COMMANDS[cmd_idx];

        /* Flush any stale RX data before sending */
        {
            uint8_t discard[256];
            while (1) {
                size_t avail = transport_uart_rx_available(uart);
                if (avail == 0) break;
                size_t to_read = avail;
                if (to_read > sizeof(discard)) to_read = sizeof(discard);
                transport_uart_rx_read(uart, discard, to_read);
            }
        }

        /* Show which command we're sending */
        snapshot_append_cmd(rx_index, cmd);

        /* Send command */
        size_t sent = transport_uart_tx_write(uart, (const uint8_t*)cmd, strlen(cmd));

        if (sent == 0) {
            snapshot_append(rx_index, "  [TX FAILED]\r\n", 16);
            all_ok = false;
            continue;
        }

        /* Wait for command spacing, then read response */
        SNAPSHOT_DELAY_MS(GNSS_SNAPSHOT_CMD_SPACING_MS);

        /* Collect response with timeout */
        uint32_t resp_start = snapshot_now_ms();
        bool got_data = false;
        uint8_t rx_buf[256];
        uint32_t quiet_ms = 0;

        while (1) {
            uint32_t elapsed = snapshot_now_ms() - resp_start;

            /* Hard timeout */
            if (elapsed >= GNSS_SNAPSHOT_RESP_TIMEOUT_MS) {
                if (!got_data) {
                    snapshot_append(rx_index, "  [TIMEOUT]\r\n", 14);
                    s_rx_timeout[rx_index] = true;
                    all_ok = false;
                }
                break;
            }

            /* Read available data */
            size_t avail = transport_uart_rx_available(uart);

            if (avail > 0) {
                size_t to_read = avail;
                if (to_read > sizeof(rx_buf)) to_read = sizeof(rx_buf);

                size_t read = transport_uart_rx_read(uart, rx_buf, to_read);

                if (read > 0) {
                    snapshot_append(rx_index, (const char*)rx_buf, read);
                    got_data = true;
                    quiet_ms = 0;
                }
            } else {
                /* Track quiet time to detect end of response */
                uint32_t quiet_step = 50;
                SNAPSHOT_DELAY_MS(quiet_step);
                quiet_ms += quiet_step;

                /* UM980 typically finishes within 200ms of last byte */
                if (got_data && quiet_ms >= 300) {
                    break;
                }
            }
        }

        /* Ensure newline after each command response */
        snapshot_append(rx_index, "\r\n", 2);

        /* Small delay between commands */
        SNAPSHOT_DELAY_MS(GNSS_SNAPSHOT_CMD_SPACING_MS);
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

    /* Receiver 1 */
    if (s_write_pos[0] > 0) {
        size_t len = s_write_pos[0];
        if (len > cap) len = cap;
        memcpy(s_combined + pos, s_snapshot[0], len);
        pos += len;
    }

    /* Separator between receivers */
    if (s_write_pos[0] > 0 && s_write_pos[1] > 0) {
        const char* sep = "\r\n\r\n========================================\r\n\r\n";
        size_t sep_len = strlen(sep);
        if (pos + sep_len < cap) {
            memcpy(s_combined + pos, sep, sep_len);
            pos += sep_len;
        }
    }

    /* Receiver 2 */
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
    s_complete = false;
    s_duration_ms = 0;

    s_initialized = true;
}

bool gnss_um980_snapshot_run_all(void* uart_primary, void* uart_secondary)
{
    if (!s_initialized) {
        ESP_LOGE("SNAPSHOT", "gnss_um980_snapshot_run_all called before init");
        return false;
    }

    uint32_t start = snapshot_now_ms();

    ESP_LOGI("SNAPSHOT", "UM980 Config Snapshot starting — receiver settle delay %d ms",
             GNSS_SNAPSHOT_SETTLE_MS);

    /* Wait for receivers to boot and stabilize */
    SNAPSHOT_DELAY_MS(GNSS_SNAPSHOT_SETTLE_MS);

    /* Query receiver 1 */
    ESP_LOGI("SNAPSHOT", "Querying UM980 Receiver 1 (UART primary)...");
    bool rx1_ok = snapshot_query_receiver(0, uart_primary);
    ESP_LOGI("SNAPSHOT", "Receiver 1: %s (%zu bytes)",
             rx1_ok ? "OK" : "TIMEOUT/ERROR", s_rx_bytes[0]);

    /* Query receiver 2 */
    ESP_LOGI("SNAPSHOT", "Querying UM980 Receiver 2 (UART secondary)...");
    bool rx2_ok = snapshot_query_receiver(1, uart_secondary);
    ESP_LOGI("SNAPSHOT", "Receiver 2: %s (%zu bytes)",
             rx2_ok ? "OK" : "TIMEOUT/ERROR", s_rx_bytes[1]);

    s_duration_ms = snapshot_now_ms() - start;
    s_complete = true;

    /* Build combined view */
    build_combined();

    ESP_LOGI("SNAPSHOT", "Config Snapshot complete in %lu ms — RX1: %s (%zu B), RX2: %s (%zu B)",
             (unsigned long)s_duration_ms,
             rx1_ok ? "OK" : "FAIL", s_rx_bytes[0],
             rx2_ok ? "OK" : "FAIL", s_rx_bytes[1]);

    return rx1_ok && rx2_ok;
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
}
