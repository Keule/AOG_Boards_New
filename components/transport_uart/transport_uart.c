#include "transport_uart.h"
#include "hal_uart.h"
#include <string.h>
#include "esp_log.h"

#ifdef __XTENSA__
/* ESP32 target: use real FreeRTOS spinlocks */
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#define UART_LOCK_DECLARE()    static portMUX_TYPE s_uart_spinlock[2] = { portMUX_INITIALIZER_UNLOCKED, portMUX_INITIALIZER_UNLOCKED }
#define UART_LOCK_INIT(uart)
#define UART_LOCK(idx)         portENTER_CRITICAL(&s_uart_spinlock[idx])
#define UART_UNLOCK(idx)       portEXIT_CRITICAL(&s_uart_spinlock[idx])
#else
/* Native/host target: no FreeRTOS spinlocks needed (single-threaded tests) */
#define UART_LOCK_DECLARE()
#define UART_LOCK_INIT(uart)
#define UART_LOCK(idx)         ((void)0)
#define UART_UNLOCK(idx)       ((void)0)
#endif

static const char* TAG = "TRANSPORT_UART";

/* Forward cast: runtime_component_t* → transport_uart_t*
 * Safe because runtime_component_t is the first field. */
static transport_uart_t* comp_to_uart(runtime_component_t* comp)
{
    return (transport_uart_t*)comp;
}

/* Forward declaration */
static void update_rates(transport_uart_t* uart, uint64_t now_us);

/* ---- Spinlock for thread-safe ring buffer access ----
 *
 * transport_uart_service_step() runs in the uart_svc FreeRTOS task.
 * transport_uart_pump() is also called from gnss_um980_control, which
 * executes inside the HTTP handler thread. Both access the same ring
 * buffers (rx_buffer.head/tail/size, tx_buffer.head/tail/size).
 * Without a lock, concurrent modification corrupts the ring state,
 * causing impossible values like rx_ring=4294839054/4096.
 *
 * The spinlock is held only during the brief HAL I/O + ring buffer
 * window (microseconds), so it does not cause priority inversion.
 *
 * On native/host builds, the lock macros compile to no-ops because
 * tests are single-threaded.
 */
UART_LOCK_DECLARE();

hal_err_t transport_uart_init(transport_uart_t* uart, const transport_uart_config_t* config)
{
    if (uart == NULL || config == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    if (!board_profile_has_uart(config->port)) {
        return HAL_ERR_NOT_SUPPORTED;
    }

    memset(uart, 0, sizeof(transport_uart_t));

    uart->port = config->port;
    uart->baudrate = config->baudrate;

    /* Init RX/TX ring buffers */
    byte_ring_buffer_init(&uart->rx_buffer, uart->rx_storage, TRANSPORT_UART_RX_BUFFER_SIZE);
    byte_ring_buffer_init(&uart->tx_buffer, uart->tx_storage, TRANSPORT_UART_TX_BUFFER_SIZE);

    /* Configure HAL UART — get pins from board profile */
    hal_uart_config_t hal_cfg = HAL_UART_CONFIG_DEFAULT();
    hal_cfg.baudrate = config->baudrate;

    board_uart_pins_t uart_pins;
    if (board_profile_get_uart_pins(config->port, &uart_pins)) {
        hal_cfg.tx_pin = uart_pins.tx_pin;
        hal_cfg.rx_pin = uart_pins.rx_pin;
    }

    hal_err_t err = hal_uart_port_init(config->port, &hal_cfg);
    if (err != HAL_OK) {
        return err;
    }

    /* Register service step callback */
    uart->component.service_step = transport_uart_service_step;

    return HAL_OK;
}

hal_err_t transport_uart_reset(transport_uart_t* uart)
{
    if (uart == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    /* Flush HAL UART */
    hal_uart_flush(uart->port);

    /* Reset ring buffers (preserving storage) */
    byte_ring_buffer_init(&uart->rx_buffer, uart->rx_storage, TRANSPORT_UART_RX_BUFFER_SIZE);
    byte_ring_buffer_init(&uart->tx_buffer, uart->tx_storage, TRANSPORT_UART_TX_BUFFER_SIZE);

    /* Zero statistics */
    memset(&uart->stats, 0, sizeof(uart->stats));

    return HAL_OK;
}

void transport_uart_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    (void)timestamp_us;
    transport_uart_t* uart = comp_to_uart(comp);
    if (uart == NULL) {
        return;
    }

    /* Update per-second rate counters (outside spinlock) */
    update_rates(uart, timestamp_us);

    /* Select spinlock based on port — 0 or 1 */
    int lock_idx = (uart->port == BOARD_UART_GNSS_SECONDARY) ? 1 : 0;

    UART_LOCK(lock_idx);

    /* ---- RX: read from HAL UART into RX buffer ---- */
    uint8_t rx_tmp[TRANSPORT_UART_BURST_SIZE];
    int n = hal_uart_read(uart->port, rx_tmp, sizeof(rx_tmp));
    if (n > 0) {
        size_t written = byte_ring_buffer_write(&uart->rx_buffer, rx_tmp, (size_t)n);
        uart->stats.rx_bytes_in += written;

        /* Track RX overflow from ring buffer */
        uint32_t rx_overflow = byte_ring_buffer_overflow_count(&uart->rx_buffer);
        if (rx_overflow > uart->stats.rx_overflow_count) {
            uart->stats.rx_overflow_count = rx_overflow;
        }
    }

    /* ---- TX: peek from ring buffer, write to HAL, consume written bytes ----
     * FIFO-safe: peek reads without removing, consume only removes
     * what HAL actually accepted.  No pushback needed. */
    size_t tx_avail = byte_ring_buffer_available(&uart->tx_buffer);
    if (tx_avail > 0) {
        size_t to_peek = tx_avail > TRANSPORT_UART_BURST_SIZE ? TRANSPORT_UART_BURST_SIZE : tx_avail;
        uint8_t tx_tmp[TRANSPORT_UART_BURST_SIZE];
        size_t peeked = byte_ring_buffer_peek(&uart->tx_buffer, tx_tmp, to_peek);

        if (peeked > 0) {
            int written = hal_uart_write(uart->port, tx_tmp, peeked);

            if (written < 0) {
                /* HAL error: do not consume any bytes */
                uart->stats.tx_errors++;
            } else if ((size_t)written < peeked) {
                /* Partial write: consume only what was actually written */
                uart->stats.tx_partial_writes++;
                byte_ring_buffer_consume(&uart->tx_buffer, (size_t)written);
                uart->stats.tx_bytes_out += (size_t)written;
            } else {
                /* Full write: consume all peeked bytes */
                byte_ring_buffer_consume(&uart->tx_buffer, (size_t)written);
                uart->stats.tx_bytes_out += (size_t)written;
            }
        }
    }

    UART_UNLOCK(lock_idx);
}

size_t transport_uart_rx_read(transport_uart_t* uart, uint8_t* buf, size_t max_len)
{
    if (uart == NULL || buf == NULL || max_len == 0) {
        return 0;
    }

    /* Use spinlock for thread safety — gnss_um980_control reads from
     * a different FreeRTOS task than the uart_svc service_step. */
    int lock_idx = (uart->port == BOARD_UART_GNSS_SECONDARY) ? 1 : 0;

    size_t result;
    UART_LOCK(lock_idx);
    result = byte_ring_buffer_read(&uart->rx_buffer, buf, max_len);
    UART_UNLOCK(lock_idx);
    return result;
}

size_t transport_uart_tx_write(transport_uart_t* uart, const uint8_t* data, size_t len)
{
    if (uart == NULL || data == NULL || len == 0) {
        return 0;
    }

    int lock_idx = (uart->port == BOARD_UART_GNSS_SECONDARY) ? 1 : 0;

    size_t written;
    uint32_t overflow_after;
    UART_LOCK(lock_idx);
    uint32_t overflow_before = byte_ring_buffer_overflow_count(&uart->tx_buffer);
    written = byte_ring_buffer_write(&uart->tx_buffer, data, len);
    overflow_after = byte_ring_buffer_overflow_count(&uart->tx_buffer);
    UART_UNLOCK(lock_idx);

    if (overflow_after > overflow_before) {
        uart->stats.tx_overflows += (overflow_after - overflow_before);
    }
    return written;
}

size_t transport_uart_rx_available(const transport_uart_t* uart)
{
    if (uart == NULL) {
        return 0;
    }

    /* Reading size from a different thread requires synchronization.
     * On ESP32 (single-core for data access), size_t reads are atomic,
     * but we use a critical section for safety with FreeRTOS. */
    int lock_idx = (uart->port == BOARD_UART_GNSS_SECONDARY) ? 1 : 0;

    size_t avail;
    UART_LOCK(lock_idx);
    avail = byte_ring_buffer_available(&uart->rx_buffer);
    UART_UNLOCK(lock_idx);
    return avail;
}

size_t transport_uart_tx_free(const transport_uart_t* uart)
{
    if (uart == NULL) {
        return 0;
    }
    int lock_idx = (uart->port == BOARD_UART_GNSS_SECONDARY) ? 1 : 0;

    size_t free_space;
    UART_LOCK(lock_idx);
    if (uart->tx_buffer.capacity <= uart->tx_buffer.size) {
        free_space = 0;
    } else {
        free_space = uart->tx_buffer.capacity - uart->tx_buffer.size;
    }
    UART_UNLOCK(lock_idx);
    return free_space;
}

const transport_uart_stats_t* transport_uart_get_stats(const transport_uart_t* uart)
{
    if (uart == NULL) {
        return NULL;
    }
    return &uart->stats;
}

/* Rate computation interval in microseconds */
#define UART_RATE_INTERVAL_US  1000000ULL  /* 1 second */

static void update_rates(transport_uart_t* uart, uint64_t now_us)
{
    uint64_t elapsed = now_us - uart->last_rate_update_us;
    if (elapsed < UART_RATE_INTERVAL_US || uart->last_rate_update_us == 0) {
        return;
    }

    double dt = (double)elapsed / 1000000.0;

    uart->stats.rx_bytes_per_s = (uint32_t)((uart->stats.rx_bytes_in - uart->stats.rx_bytes_in_prev) / dt);
    uart->stats.rx_overruns_per_s = (uint32_t)((uart->stats.rx_overflow_count - uart->stats.rx_overflow_count_prev) / dt);

    uart->stats.rx_bytes_in_prev = uart->stats.rx_bytes_in;
    uart->stats.rx_overflow_count_prev = uart->stats.rx_overflow_count;
    uart->last_rate_update_us = now_us;
}

void transport_uart_pump(transport_uart_t* uart)
{
    if (uart == NULL) {
        return;
    }
    /* Delegate to service_step — safe because runtime_component_t is the first field.
     * Timestamp 0 is harmless (not used by the UART transport).
     *
     * IMPORTANT: service_step now uses spinlock internally, so this is
     * thread-safe even when called from the HTTP handler context while
     * the uart_svc task also calls service_step periodically. */
    transport_uart_service_step(&uart->component, 0);
}

hal_err_t transport_uart_diagnostics(const transport_uart_t* uart, transport_uart_diagnostics_t* diag)
{
    if (uart == NULL || diag == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    /* Read ring buffer state under spinlock for consistency */
    int lock_idx = (uart->port == BOARD_UART_GNSS_SECONDARY) ? 1 : 0;

    uint32_t cap, used;
    UART_LOCK(lock_idx);
    cap = (uint32_t)uart->rx_buffer.capacity;
    used = (uint32_t)uart->rx_buffer.size;
    UART_UNLOCK(lock_idx);

    /* Saturate: used must never exceed capacity */
    if (used > cap) {
        diag->rx_buffer_used = cap;  /* clamp to capacity */
        /* Increment stat error counter — cast away const because this is
         * a diagnostic anomaly counter, not functional state. */
        transport_uart_t* mut = (transport_uart_t*)uart;
        if (mut->stats.rx_ring_stat_errors < UINT32_MAX) {
            mut->stats.rx_ring_stat_errors++;
        }
    } else {
        diag->rx_buffer_used = used;
    }

    diag->rx_buffer_size = cap;
    diag->rx_buffer_free = cap - diag->rx_buffer_used;
    diag->rx_overflow_total = byte_ring_buffer_overflow_count(&uart->rx_buffer);

    uint32_t tcap;
    uint32_t tused;
    UART_LOCK(lock_idx);
    tcap = (uint32_t)uart->tx_buffer.capacity;
    tused = (uint32_t)uart->tx_buffer.size;
    UART_UNLOCK(lock_idx);

    diag->tx_buffer_size = tcap;
    diag->tx_buffer_used = (tused > tcap) ? tcap : tused;  /* BUG FIX: was (tused > tcap) ? tcap : tcap */
    diag->tx_buffer_free = tcap - diag->tx_buffer_used;

    diag->stats = uart->stats;

    return HAL_OK;
}

/* ---- Clamped ring buffer occupancy (thread-safe) ----
 * Returns rx_buffer.size clamped to [0, capacity].
 * Safe to call from any context, including hw_runtime_diag. */
uint32_t transport_uart_rx_ring_used(const transport_uart_t* uart)
{
    if (uart == NULL) return 0;

    int lock_idx = (uart->port == BOARD_UART_GNSS_SECONDARY) ? 1 : 0;

    uint32_t cap, used;
    UART_LOCK(lock_idx);
    cap = (uint32_t)uart->rx_buffer.capacity;
    used = (uint32_t)uart->rx_buffer.size;
    UART_UNLOCK(lock_idx);

    if (used > cap) {
        /* Log anomaly once */
        transport_uart_t* mut = (transport_uart_t*)uart;
        if (mut->stats.rx_ring_stat_errors < UINT32_MAX) {
            mut->stats.rx_ring_stat_errors++;
        }
        return cap;
    }
    return used;
}
