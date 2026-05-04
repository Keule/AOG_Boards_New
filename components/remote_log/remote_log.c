/* ========================================================================
 * remote_log.c — In-memory ringbuffer for ESP_LOG output
 *
 * NAV-REMOTE-LOG-001:
 *   Intercepts ALL ESP_LOG output via esp_log_set_vprintf() and stores it
 *   in a statically allocated ringbuffer. Accessible via HTTP endpoints.
 *
 * NAV-REMOTELOG-BOOTBUFFER-001:
 *   Two-phase init:
 *     Phase 1 — remote_log_early_init(): Called in app_main() before
 *               FreeRTOS scheduler starts. Uses critical sections for
 *               locking. No malloc, no FreeRTOS primitives needed.
 *     Phase 2 — remote_log_init():     Called after scheduler is running.
 *               Creates mutex, transfers boot buffer into main ringbuffer,
 *               upgrades vprintf hook from critical-section locking to
 *               try-lock mutex.
 *
 *   Boot buffer is a separate static 16KB buffer used only during early
 *   phase. On init(), its contents are copied into the main 32KB buffer
 *   and the early phase is marked complete.
 *
 * CRITICAL RULES for the vprintf hook:
 *   - Uses va_copy() before consuming va_list (can only be consumed once)
 *   - NEVER calls ESP_LOG*() — would cause infinite recursion
 *   - NEVER does any HTTP/network/malloc operations per log line
 *   - Early phase: uses portENTER_CRITICAL() / portEXIT_CRITICAL()
 *   - Late phase: uses xSemaphoreTryTake() with timeout=0
 *   - NEVER blocks on mutex
 *
 * Ringbuffer behavior:
 *   - Overwrites old data when full. Tracks bytes_overwritten.
 *   - tail_lines: counts newlines backwards from write position.
 *
 * Memory:
 *   - 32KB main buffer + 16KB boot buffer = 48KB static RAM
 *   - After init(), boot buffer memory is reused (no waste)
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
#define REMOTE_LOG_BUFFER_SIZE      32768   /* Main ringbuffer (32 KB) */
#define REMOTE_LOG_BOOT_BUF_SIZE    16384   /* Early-init ringbuffer (16 KB) */
#define REMOTE_LOG_LINE_MAX         384

/* =================================================================
 * Phase tracking
 * ================================================================= */

/* 0 = not started, 1 = early init done, 2 = full init done */
static volatile int s_init_phase = 0;

/* ---- Early-phase boot buffer (before FreeRTOS mutex available) ---- */
static char s_boot_buffer[REMOTE_LOG_BOOT_BUF_SIZE];
static volatile size_t s_boot_write_pos;
static volatile size_t s_boot_used;
static volatile uint32_t s_boot_lines_captured;
static volatile uint32_t s_boot_bytes_captured;

/* ---- Main ringbuffer (active after init()) ---- */
static char s_ringbuffer[REMOTE_LOG_BUFFER_SIZE];

/* ---- Ringbuffer state ---- */
static volatile size_t s_write_pos;     /* Next byte to write */
static volatile size_t s_used_bytes;    /* Bytes in buffer (min of written, capacity) */

/* ---- Statistics ---- */
static volatile uint32_t s_lines_written;
static volatile uint32_t s_lines_dropped;
static volatile uint32_t s_bytes_written;
static volatile uint32_t s_bytes_overwritten;

/* ---- Mutex (created in phase 2) ---- */
static SemaphoreHandle_t s_buffer_mutex = NULL;

/* ---- Original vprintf (saved before hooking) ---- */
static vprintf_like_t s_original_vprintf = NULL;

/* ---- Critical section spinlock (ESP-IDF 5.x requires mutex arg) ---- */
static portMUX_TYPE s_critical_lock = portMUX_INITIALIZER_UNLOCKED;

/* ---- Recursion guard ----
 * If the vprintf hook itself triggers a log (e.g., via a crashed
 * syscall or an assert in vsnprintf), we must not recurse. */
static volatile bool s_in_hook = false;

/* =================================================================
 * Internal: append data to any ringbuffer (no locking — caller handles it)
 * ================================================================= */
static void ringbuffer_append(char* buf, size_t buf_size,
                              volatile size_t* write_pos,
                              volatile size_t* used,
                              const char* data, size_t len)
{
    if (data == NULL || len == 0) return;

    if (len > buf_size) {
        data += (len - buf_size);
        len = buf_size;
        *write_pos = 0;
        *used = 0;
    }

    /* Case 1: Fits without wrapping */
    if (*write_pos + len <= buf_size) {
        memcpy(buf + *write_pos, data, len);
        *write_pos += len;
    } else {
        /* Case 2: Wraps around */
        size_t first_chunk = buf_size - *write_pos;
        memcpy(buf + *write_pos, data, first_chunk);
        size_t second_chunk = len - first_chunk;
        memcpy(buf, data + first_chunk, second_chunk);
        *write_pos = second_chunk;
    }

    /* Update used (capped at capacity) */
    if (*write_pos > *used) {
        *used = *write_pos;
    }
    if (*used > buf_size) {
        *used = buf_size;
    }
}

/* =================================================================
 * Early-phase append (critical section, before mutex exists)
 * ================================================================= */
static void early_append(const char* data, size_t len)
{
    portENTER_CRITICAL(&s_critical_lock);
    ringbuffer_append(s_boot_buffer, REMOTE_LOG_BOOT_BUF_SIZE,
                      &s_boot_write_pos, &s_boot_used,
                      data, len);
    s_boot_lines_captured++;
    s_boot_bytes_captured += (uint32_t)len;
    if (s_boot_bytes_captured > REMOTE_LOG_BOOT_BUF_SIZE) {
        /* Count overflows (boot buffer is circular too) */
        s_bytes_overwritten += (s_boot_bytes_captured - REMOTE_LOG_BOOT_BUF_SIZE);
        /* Reset to avoid unbounded growth of the counter math */
        s_boot_bytes_captured = REMOTE_LOG_BOOT_BUF_SIZE;
    }
    portEXIT_CRITICAL(&s_critical_lock);
}

/* =================================================================
 * Late-phase append (mutex try-lock, non-blocking)
 * ================================================================= */
static void late_append(const char* data, size_t len)
{
    if (xSemaphoreTake(s_buffer_mutex, 0) == pdTRUE) {
        ringbuffer_append(s_ringbuffer, REMOTE_LOG_BUFFER_SIZE,
                          &s_write_pos, &s_used_bytes,
                          data, len);

        /* Track overflow */
        s_bytes_written += (uint32_t)len;
        s_lines_written++;
        if (s_bytes_written > REMOTE_LOG_BUFFER_SIZE) {
            s_bytes_overwritten += (uint32_t)len;
        }

        xSemaphoreGive(s_buffer_mutex);
    } else {
        s_lines_dropped++;
    }
}

/* =================================================================
 * vprintf hook — intercepts ALL log output
 *
 * Routes to early_append() or late_append() depending on init phase.
 * CRITICAL: Never calls ESP_LOG*(), never blocks, never allocates.
 * ================================================================= */
static int remote_log_vprintf(const char* fmt, va_list ap)
{
    /* Recursion guard — if we're already in the hook, bail out */
    if (s_in_hook) {
        /* Still forward to original to avoid losing UART output */
        if (s_original_vprintf != NULL) {
            return s_original_vprintf(fmt, ap);
        }
        return 0;
    }
    s_in_hook = true;

    char line[REMOTE_LOG_LINE_MAX];
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int n = vsnprintf(line, sizeof(line), fmt, ap_copy);
    va_end(ap_copy);

    if (n > 0) {
        size_t write_len = (size_t)n;
        if (write_len >= sizeof(line)) {
            write_len = sizeof(line) - 1;
        }

        if (s_init_phase == 1) {
            /* Early phase: use critical section */
            early_append(line, write_len);
        } else if (s_init_phase == 2) {
            /* Late phase: use mutex try-lock */
            late_append(line, write_len);
        }
        /* phase == 0 should never reach here (hook not installed) */
    }

    s_in_hook = false;

    /* Forward to original logger (UART output) */
    if (s_original_vprintf != NULL) {
        return s_original_vprintf(fmt, ap);
    }
    return n;
}

/* =================================================================
 * Phase 1: Early Init (before FreeRTOS scheduler)
 *
 * Safe to call at the very top of app_main(), before any subsystem.
 * No FreeRTOS primitives are used — only critical sections.
 * ================================================================= */
void remote_log_early_init(void)
{
    if (s_init_phase >= 1) return;  /* Already initialized */

    /* Clear boot buffer state */
    memset(s_boot_buffer, 0, sizeof(s_boot_buffer));
    s_boot_write_pos = 0;
    s_boot_used = 0;
    s_boot_lines_captured = 0;
    s_boot_bytes_captured = 0;

    /* Clear main buffer state (will be filled on init()) */
    memset(s_ringbuffer, 0, sizeof(s_ringbuffer));
    s_write_pos = 0;
    s_used_bytes = 0;

    /* Clear stats */
    s_lines_written = 0;
    s_lines_dropped = 0;
    s_bytes_written = 0;
    s_bytes_overwritten = 0;

    /* Install vprintf hook — captures everything from here on */
    s_original_vprintf = esp_log_set_vprintf(remote_log_vprintf);

    s_init_phase = 1;
}

/* =================================================================
 * Phase 2: Full Init (after scheduler is running)
 *
 * Creates mutex, transfers boot buffer into main ringbuffer,
 * upgrades locking to mutex-based.
 * ================================================================= */
void remote_log_init(void)
{
    /* If early_init was never called, do a full init from scratch */
    if (s_init_phase == 0) {
        remote_log_early_init();
    }

    if (s_init_phase >= 2) return;  /* Already fully initialized */

    /* Create mutex */
    s_buffer_mutex = xSemaphoreCreateMutex();
    if (s_buffer_mutex == NULL) {
        return;  /* Cannot proceed without mutex — boot buffer still works */
    }

    /* ---- Transfer boot buffer into main ringbuffer ---- */
    if (s_boot_used > 0) {
        size_t boot_data_len = s_boot_used;
        if (boot_data_len > REMOTE_LOG_BOOT_BUF_SIZE) {
            boot_data_len = REMOTE_LOG_BOOT_BUF_SIZE;
        }

        /* Determine read start in boot buffer (oldest data) */
        size_t boot_read_start;
        if (s_boot_used < REMOTE_LOG_BOOT_BUF_SIZE) {
            boot_read_start = 0;
        } else {
            boot_read_start = s_boot_write_pos;  /* wrap point = oldest */
        }

        /* Copy boot buffer content into main ringbuffer */
        size_t remaining = boot_data_len;
        size_t rpos = boot_read_start;

        while (remaining > 0) {
            size_t avail = REMOTE_LOG_BOOT_BUF_SIZE - rpos;
            size_t chunk = (remaining < avail) ? remaining : avail;
            if (chunk > REMOTE_LOG_BUFFER_SIZE - s_write_pos) {
                chunk = REMOTE_LOG_BUFFER_SIZE - s_write_pos;
            }
            if (chunk == 0) {
                /* Main buffer segment full, wrap */
                /* ringbuffer_append handles wrapping internally */
                break;
            }
            memcpy(s_ringbuffer + s_write_pos, s_boot_buffer + rpos, chunk);
            s_write_pos += chunk;
            if (s_write_pos >= REMOTE_LOG_BUFFER_SIZE) {
                s_write_pos = 0;
            }
            rpos = (rpos + chunk) % REMOTE_LOG_BOOT_BUF_SIZE;
            remaining -= chunk;
        }

        s_used_bytes = s_write_pos;
        if (s_used_bytes > REMOTE_LOG_BUFFER_SIZE) {
            s_used_bytes = REMOTE_LOG_BUFFER_SIZE;
        }

        /* Transfer boot stats into main stats */
        s_lines_written = s_boot_lines_captured;
        s_bytes_written = (s_boot_bytes_captured > REMOTE_LOG_BOOT_BUF_SIZE)
                          ? REMOTE_LOG_BOOT_BUF_SIZE
                          : s_boot_bytes_captured;

        /* If boot data was larger than main buffer, count overflow */
        if (s_boot_bytes_captured > REMOTE_LOG_BUFFER_SIZE) {
            s_bytes_overwritten = (uint32_t)(s_boot_bytes_captured - REMOTE_LOG_BUFFER_SIZE);
        }
    }

    /* Clear boot buffer — no longer needed, memory is static (no reclaim) */
    memset(s_boot_buffer, 0, sizeof(s_boot_buffer));
    s_boot_write_pos = 0;
    s_boot_used = 0;

    /* Upgrade to fully initialized — vprintf hook now uses mutex */
    s_init_phase = 2;
}

bool remote_log_is_initialized(void)
{
    return (s_init_phase >= 1);
}

/* =================================================================
 * Read full buffer content (oldest → newest)
 * ================================================================= */
size_t remote_log_read(char* out, size_t out_size)
{
    if (s_init_phase < 1 || out == NULL || out_size == 0) return 0;

    /* In early phase, read from boot buffer with critical section */
    if (s_init_phase == 1) {
        portENTER_CRITICAL(&s_critical_lock);
        size_t used = s_boot_used;
        size_t write_pos = s_boot_write_pos;
        portEXIT_CRITICAL(&s_critical_lock);

        if (used == 0) {
            out[0] = '\0';
            return 0;
        }

        size_t to_copy = used;
        if (to_copy > out_size - 1) to_copy = out_size - 1;

        size_t read_start = (used < REMOTE_LOG_BOOT_BUF_SIZE) ? 0 : write_pos;
        size_t copied = 0;
        size_t rpos = read_start;

        while (copied < to_copy) {
            size_t avail = REMOTE_LOG_BOOT_BUF_SIZE - rpos;
            size_t chunk = (to_copy - copied < avail) ? (to_copy - copied) : avail;
            memcpy(out + copied, s_boot_buffer + rpos, chunk);
            copied += chunk;
            rpos = (rpos + chunk) % REMOTE_LOG_BOOT_BUF_SIZE;
        }
        out[copied] = '\0';
        return copied;
    }

    /* Phase 2: read from main ringbuffer with mutex */
    xSemaphoreTake(s_buffer_mutex, portMAX_DELAY);

    size_t to_copy = s_used_bytes;
    if (to_copy == 0) {
        xSemaphoreGive(s_buffer_mutex);
        out[0] = '\0';
        return 0;
    }
    if (to_copy > out_size - 1) to_copy = out_size - 1;

    size_t read_start = (s_used_bytes < REMOTE_LOG_BUFFER_SIZE) ? 0 : s_write_pos;

    size_t copied = 0;
    size_t rpos = read_start;
    while (copied < to_copy) {
        size_t avail = REMOTE_LOG_BUFFER_SIZE - rpos;
        size_t chunk = (to_copy - copied < avail) ? (to_copy - copied) : avail;
        memcpy(out + copied, s_ringbuffer + rpos, chunk);
        copied += chunk;
        rpos = (rpos + chunk) % REMOTE_LOG_BUFFER_SIZE;
    }

    out[copied] = '\0';
    xSemaphoreGive(s_buffer_mutex);
    return copied;
}

/* =================================================================
 * Read last N lines from buffer
 * ================================================================= */
size_t remote_log_read_tail_lines(char* out, size_t out_size, uint32_t max_lines)
{
    if (s_init_phase < 1 || out == NULL || out_size == 0) return 0;
    if (max_lines == 0) max_lines = 50;

    /* Select buffer based on phase */
    char* buf;
    size_t buf_size;
    size_t used;
    size_t write_pos;

    if (s_init_phase == 1) {
        buf = s_boot_buffer;
        buf_size = REMOTE_LOG_BOOT_BUF_SIZE;
        portENTER_CRITICAL(&s_critical_lock);
        used = s_boot_used;
        write_pos = s_boot_write_pos;
        portEXIT_CRITICAL(&s_critical_lock);
    } else {
        buf = s_ringbuffer;
        buf_size = REMOTE_LOG_BUFFER_SIZE;
        xSemaphoreTake(s_buffer_mutex, portMAX_DELAY);
        used = s_used_bytes;
        write_pos = s_write_pos;
    }

    if (used == 0) {
        if (s_init_phase == 2) xSemaphoreGive(s_buffer_mutex);
        out[0] = '\0';
        return 0;
    }

    /* Scan backwards from write_pos counting newlines */
    size_t scan_pos = (write_pos == 0) ? buf_size - 1 : write_pos - 1;
    uint32_t newline_count = 0;
    size_t start_pos = 0;
    size_t bytes_scanned = 0;
    size_t total_data = (used < buf_size) ? used : buf_size;

    while (bytes_scanned < total_data) {
        if (buf[scan_pos] == '\n') {
            newline_count++;
            if (newline_count >= max_lines) {
                start_pos = (scan_pos + 1) % buf_size;
                break;
            }
        }
        if (scan_pos == 0) {
            scan_pos = buf_size - 1;
        } else {
            scan_pos--;
        }
        bytes_scanned++;

        if (bytes_scanned >= total_data) {
            start_pos = (used < buf_size) ? 0 : ((write_pos + 1) % buf_size);
        }
    }

    if (newline_count == 0) {
        start_pos = (used < buf_size) ? 0 : ((write_pos + 1) % buf_size);
    }

    /* Calculate data length from start_pos to write_pos */
    size_t data_len;
    if (used < buf_size) {
        data_len = write_pos - start_pos;
    } else {
        data_len = (write_pos >= start_pos)
                   ? (write_pos - start_pos)
                   : (buf_size - start_pos + write_pos);
    }

    size_t to_copy = (data_len < out_size - 1) ? data_len : (out_size - 1);

    size_t copied = 0;
    size_t rpos = start_pos;
    while (copied < to_copy) {
        size_t avail = buf_size - rpos;
        size_t chunk = (to_copy - copied < avail) ? (to_copy - copied) : avail;
        memcpy(out + copied, buf + rpos, chunk);
        copied += chunk;
        rpos = (rpos + chunk) % buf_size;
    }

    out[copied] = '\0';

    if (s_init_phase == 2) xSemaphoreGive(s_buffer_mutex);
    return copied;
}

/* =================================================================
 * Clear buffer
 * ================================================================= */
void remote_log_clear(void)
{
    if (s_init_phase < 1) return;

    if (s_init_phase == 1) {
        portENTER_CRITICAL(&s_critical_lock);
        memset(s_boot_buffer, 0, sizeof(s_boot_buffer));
        s_boot_write_pos = 0;
        s_boot_used = 0;
        s_boot_lines_captured = 0;
        s_boot_bytes_captured = 0;
        s_bytes_overwritten = 0;
        portEXIT_CRITICAL(&s_critical_lock);
        return;
    }

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

/* =================================================================
 * Get statistics (extended with boot-buffer info)
 * ================================================================= */
void remote_log_get_stats(remote_log_stats_t* out)
{
    if (out == NULL) return;

    if (s_init_phase < 1) {
        memset(out, 0, sizeof(*out));
        out->buffer_size = REMOTE_LOG_BUFFER_SIZE;
        return;
    }

    out->early_init_called = (s_init_phase >= 1);
    out->fully_initialized = (s_init_phase >= 2);

    if (s_init_phase == 1) {
        portENTER_CRITICAL(&s_critical_lock);
        out->lines_written = s_boot_lines_captured;
        out->lines_dropped = s_lines_dropped;
        out->bytes_written = s_boot_bytes_captured;
        out->bytes_overwritten = s_bytes_overwritten;
        out->buffer_size = REMOTE_LOG_BOOT_BUF_SIZE;
        out->used_bytes = s_boot_used;
        out->boot_lines_captured = s_boot_lines_captured;
        out->boot_bytes_captured = s_boot_bytes_captured;
        portEXIT_CRITICAL(&s_critical_lock);
    } else {
        xSemaphoreTake(s_buffer_mutex, portMAX_DELAY);
        out->lines_written = s_lines_written;
        out->lines_dropped = s_lines_dropped;
        out->bytes_written = s_bytes_written;
        out->bytes_overwritten = s_bytes_overwritten;
        out->buffer_size = REMOTE_LOG_BUFFER_SIZE;
        out->used_bytes = s_used_bytes;
        out->boot_lines_captured = s_boot_lines_captured;
        out->boot_bytes_captured = s_boot_bytes_captured;
        xSemaphoreGive(s_buffer_mutex);
    }
}
