/* ========================================================================
 * remote_log.c — In-memory ringbuffer for ESP_LOG output (NAV-REMOTE-LOG-001)
 *
 * Intercepts ALL ESP_LOG output via esp_log_set_vprintf() and stores it
 * in a statically allocated 32KB ringbuffer. The buffer is accessible
 * via HTTP endpoints (/logs, /logs?tail=N, /logs/status, /logs/clear).
 *
 * CRITICAL RULES for the vprintf hook:
 *   - Uses va_copy() before consuming va_list (can only be consumed once)
 *   - NEVER calls ESP_LOG*() — would cause infinite recursion
 *   - NEVER does any HTTP/network/malloc operations per log line
 *   - Uses try-lock (non-blocking) — drops lines if buffer is locked
 *   - NEVER blocks on mutex — uses xSemaphoreTryTake() with timeout=0
 *
 * Ringbuffer behavior:
 *   - Overwrites old data when full. Tracks bytes_overwritten.
 *   - tail_lines: counts newlines backwards from write position
 *
 * Memory:
 *   - 32KB statically allocated (not heap)
 *   - ~32KB total static RAM cost
 * ======================================================================== */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "remote_log.h"

/* ---- Configuration ---- */
#define REMOTE_LOG_BUFFER_SIZE  32768
#define REMOTE_LOG_LINE_MAX     384

/* ---- Static ringbuffer ---- */
static char s_ringbuffer[REMOTE_LOG_BUFFER_SIZE];

/* ---- Ringbuffer state ---- */
static volatile size_t s_write_pos;     /* Next byte to write */
static volatile size_t s_used_bytes;    /* Bytes in buffer (min of written, capacity) */

/* ---- Statistics ---- */
static volatile uint32_t s_lines_written;
static volatile uint32_t s_lines_dropped;
static volatile uint32_t s_bytes_written;
static volatile uint32_t s_bytes_overwritten;

/* ---- Mutex for buffer access ---- */
static SemaphoreHandle_t s_buffer_mutex = NULL;

/* ---- Initialization flag ---- */
static volatile bool s_initialized = false;

/* ---- Original vprintf (saved before hooking) ---- */
static vprintf_like_t s_original_vprintf = NULL;

/* =================================================================
 * Internal: append data to ringbuffer (caller MUST hold mutex)
 * ================================================================= */
static void remote_log_append_internal(const char* data, size_t len)
{
    if (data == NULL || len == 0) return;

    size_t avail = REMOTE_LOG_BUFFER_SIZE;

    if (len > avail) {
        /* Line longer than entire buffer — truncate to last 'avail' bytes */
        s_bytes_overwritten += (uint32_t)(len - avail);
        data += (len - avail);
        len = avail;
        s_write_pos = 0;
        s_used_bytes = 0;
    }

    /* Case 1: Fits without wrapping */
    if (s_write_pos + len <= avail) {
        memcpy(s_ringbuffer + s_write_pos, data, len);
        s_write_pos += len;
    } else {
        /* Case 2: Wraps around */
        size_t first_chunk = avail - s_write_pos;
        memcpy(s_ringbuffer + s_write_pos, data, first_chunk);
        size_t second_chunk = len - first_chunk;
        memcpy(s_ringbuffer, data + first_chunk, second_chunk);
        s_write_pos = second_chunk;
    }

    /* Update used bytes and overwritten count */
    size_t new_total = s_bytes_written + len;
    if (new_total > avail) {
        if (s_bytes_written <= avail) {
            /* Just crossed capacity */
            s_bytes_overwritten += (uint32_t)(new_total - avail);
        } else {
            s_bytes_overwritten += (uint32_t)len;
        }
        s_used_bytes = avail;
    } else {
        s_used_bytes = new_total;
    }

    s_bytes_written += (uint32_t)len;
    s_lines_written++;
}

/* =================================================================
 * vprintf hook — intercepts ALL log output
 *
 * CRITICAL: This function must be as fast as possible.
 * - No ESP_LOG*() calls (infinite recursion)
 * - No malloc/free
 * - No network operations
 * - Non-blocking mutex (drop line if locked)
 * ================================================================= */
static int remote_log_vprintf(const char* fmt, va_list ap)
{
    char line[REMOTE_LOG_LINE_MAX];

    /* Use va_copy because va_list can only be consumed once */
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int n = vsnprintf(line, sizeof(line), fmt, ap_copy);
    va_end(ap_copy);

    if (n > 0) {
        /* Try to lock, non-blocking — drop line if buffer is busy */
        if (xSemaphoreTake(s_buffer_mutex, 0) == pdTRUE) {
            size_t write_len = (size_t)n;
            if (write_len >= sizeof(line)) {
                write_len = sizeof(line) - 1;
            }
            remote_log_append_internal(line, write_len);
            xSemaphoreGive(s_buffer_mutex);
        } else {
            /* Buffer locked, drop this line */
            s_lines_dropped++;
        }
    }

    /* Forward to original logger (UART output) */
    if (s_original_vprintf != NULL) {
        return s_original_vprintf(fmt, ap);
    }
    return n;
}

/* =================================================================
 * Public API
 * ================================================================= */

void remote_log_init(void)
{
    if (s_initialized) return;

    /* Create mutex */
    s_buffer_mutex = xSemaphoreCreateMutex();
    if (s_buffer_mutex == NULL) {
        return;  /* Cannot init without mutex */
    }

    /* Reset state */
    memset(s_ringbuffer, 0, sizeof(s_ringbuffer));
    s_write_pos = 0;
    s_used_bytes = 0;
    s_lines_written = 0;
    s_lines_dropped = 0;
    s_bytes_written = 0;
    s_bytes_overwritten = 0;

    /* Hook into ESP log system */
    s_original_vprintf = esp_log_set_vprintf(remote_log_vprintf);

    s_initialized = true;
}

bool remote_log_is_initialized(void)
{
    return s_initialized;
}

size_t remote_log_read(char* out, size_t out_size)
{
    if (!s_initialized || out == NULL || out_size == 0) return 0;

    xSemaphoreTake(s_buffer_mutex, portMAX_DELAY);

    size_t to_copy = s_used_bytes;
    if (to_copy == 0) {
        xSemaphoreGive(s_buffer_mutex);
        out[0] = '\0';
        return 0;
    }

    if (to_copy > out_size - 1) {
        to_copy = out_size - 1;
    }

    /* If buffer is not full, data starts at 0 */
    /* If buffer is full, oldest data is at s_write_pos */
    size_t read_start;
    if (s_used_bytes < REMOTE_LOG_BUFFER_SIZE) {
        read_start = 0;
    } else {
        read_start = s_write_pos;  /* wrap point = oldest data */
    }

    /* Copy data (may wrap) */
    size_t copied = 0;
    while (copied < to_copy) {
        size_t avail_from_pos = REMOTE_LOG_BUFFER_SIZE - read_start;
        size_t chunk = to_copy - copied;
        if (chunk > avail_from_pos) {
            chunk = avail_from_pos;
        }
        memcpy(out + copied, s_ringbuffer + read_start, chunk);
        copied += chunk;
        read_start = (read_start + chunk) % REMOTE_LOG_BUFFER_SIZE;
    }

    out[copied] = '\0';
    xSemaphoreGive(s_buffer_mutex);
    return copied;
}

size_t remote_log_read_tail_lines(char* out, size_t out_size, uint32_t max_lines)
{
    if (!s_initialized || out == NULL || out_size == 0) return 0;
    if (max_lines == 0) max_lines = 50;  /* sensible default */

    xSemaphoreTake(s_buffer_mutex, portMAX_DELAY);

    if (s_used_bytes == 0) {
        xSemaphoreGive(s_buffer_mutex);
        out[0] = '\0';
        return 0;
    }

    /*
     * Strategy: scan backwards from s_write_pos counting newlines.
     * Stop when we've found max_lines newlines or reached the start.
     * Then copy from that position to s_write_pos.
     */
    size_t scan_pos = (s_write_pos == 0) ? REMOTE_LOG_BUFFER_SIZE - 1 : s_write_pos - 1;
    uint32_t newline_count = 0;
    size_t start_pos = 0;

    /* We always want at least the data up to the write position */
    size_t total_data = s_used_bytes;
    if (total_data > REMOTE_LOG_BUFFER_SIZE) total_data = REMOTE_LOG_BUFFER_SIZE;

    /* Scan backwards */
    size_t bytes_scanned = 0;
    while (bytes_scanned < total_data) {
        if (s_ringbuffer[scan_pos] == '\n') {
            newline_count++;
            if (newline_count >= max_lines) {
                /* Start after this newline */
                start_pos = (scan_pos + 1) % REMOTE_LOG_BUFFER_SIZE;
                break;
            }
        }
        if (scan_pos == 0) {
            scan_pos = REMOTE_LOG_BUFFER_SIZE - 1;
        } else {
            scan_pos--;
        }
        bytes_scanned++;

        if (bytes_scanned >= total_data) {
            /* Didn't find enough newlines — start from beginning of valid data */
            if (s_used_bytes < REMOTE_LOG_BUFFER_SIZE) {
                start_pos = 0;
            } else {
                start_pos = (s_write_pos + 1) % REMOTE_LOG_BUFFER_SIZE;
            }
        }
    }

    /* If we didn't find any newlines, return everything */
    if (newline_count == 0) {
        if (s_used_bytes < REMOTE_LOG_BUFFER_SIZE) {
            start_pos = 0;
        } else {
            start_pos = (s_write_pos + 1) % REMOTE_LOG_BUFFER_SIZE;
        }
    }

    /* Calculate how many bytes from start_pos to s_write_pos */
    size_t data_len;
    if (s_used_bytes < REMOTE_LOG_BUFFER_SIZE) {
        data_len = s_write_pos - start_pos;
    } else {
        /* Buffer is full: data wraps from start_pos through s_write_pos */
        if (s_write_pos >= start_pos) {
            data_len = s_write_pos - start_pos;
        } else {
            data_len = REMOTE_LOG_BUFFER_SIZE - start_pos + s_write_pos;
        }
    }

    /* Copy with wrap handling */
    size_t to_copy = data_len;
    if (to_copy > out_size - 1) {
        to_copy = out_size - 1;
    }

    size_t copied = 0;
    size_t rpos = start_pos;
    while (copied < to_copy) {
        size_t avail_from_pos = REMOTE_LOG_BUFFER_SIZE - rpos;
        size_t chunk = to_copy - copied;
        if (chunk > avail_from_pos) {
            chunk = avail_from_pos;
        }
        memcpy(out + copied, s_ringbuffer + rpos, chunk);
        copied += chunk;
        rpos = (rpos + chunk) % REMOTE_LOG_BUFFER_SIZE;
    }

    out[copied] = '\0';
    xSemaphoreGive(s_buffer_mutex);
    return copied;
}

void remote_log_clear(void)
{
    if (!s_initialized) return;

    xSemaphoreTake(s_buffer_mutex, portMAX_DELAY);
    memset(s_ringbuffer, 0, sizeof(s_ringbuffer));
    s_write_pos = 0;
    s_used_bytes = 0;
    s_lines_written = 0;
    s_lines_dropped = 0;
    s_bytes_written = 0;
    s_bytes_overwritten = 0;
    xSemaphoreGive(s_buffer_mutex);
}

void remote_log_get_stats(remote_log_stats_t* out)
{
    if (out == NULL) return;

    if (!s_initialized) {
        memset(out, 0, sizeof(*out));
        out->buffer_size = REMOTE_LOG_BUFFER_SIZE;
        return;
    }

    xSemaphoreTake(s_buffer_mutex, portMAX_DELAY);
    out->lines_written = s_lines_written;
    out->lines_dropped = s_lines_dropped;
    out->bytes_written = s_bytes_written;
    out->bytes_overwritten = s_bytes_overwritten;
    out->buffer_size = REMOTE_LOG_BUFFER_SIZE;
    out->used_bytes = s_used_bytes;
    xSemaphoreGive(s_buffer_mutex);
}
