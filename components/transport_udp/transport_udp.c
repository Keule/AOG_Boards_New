#include "transport_udp.h"
#include <string.h>

/* ---- Internal helpers ---- */

static transport_udp_t* comp_to_udp(runtime_component_t* comp)
{
    return (transport_udp_t*)comp;
}

/* Stub HAL ops — do nothing, return success */
static hal_err_t stub_bind(uint16_t port)      { (void)port; return HAL_OK; }
static hal_err_t stub_close(void)               { return HAL_OK; }
static int stub_recvfrom(uint8_t* buf, size_t len, uint32_t* ip, uint16_t* port)
    { (void)buf; (void)len; (void)ip; (void)port; return 0; }
static int stub_sendto(const uint8_t* buf, size_t len, uint32_t ip, uint16_t port)
    { (void)buf; (void)len; (void)ip; (void)port; return 0; }
static bool stub_is_bound(void)                  { return false; }

static const transport_udp_hal_ops_t s_stub_hal = {
    .bind      = stub_bind,
    .close     = stub_close,
    .recvfrom  = stub_recvfrom,
    .sendto    = stub_sendto,
    .is_bound  = stub_is_bound,
};

/* ---- Init / Deinit ---- */

hal_err_t transport_udp_init(transport_udp_t* udp, const transport_udp_config_t* config)
{
    if (udp == NULL || config == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    memset(udp, 0, sizeof(transport_udp_t));
    udp->local_port  = config->local_port;
    udp->remote_ip   = config->remote_ip;
    udp->remote_port = config->remote_port;
    udp->hal         = &s_stub_hal;

    byte_ring_buffer_init(&udp->rx_buffer, udp->rx_storage, sizeof(udp->rx_storage));
    byte_ring_buffer_init(&udp->tx_buffer, udp->tx_storage, sizeof(udp->tx_storage));

    /* Bind local port via HAL */
    if (udp->hal != NULL && udp->hal->bind != NULL) {
        hal_err_t err = udp->hal->bind(config->local_port);
        if (err == HAL_OK) {
            udp->bound = true;
        }
    }

    /* Register service step callback */
    udp->component.service_step = transport_udp_service_step;

    return HAL_OK;
}

hal_err_t transport_udp_deinit(transport_udp_t* udp)
{
    if (udp == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    if (udp->bound && udp->hal != NULL && udp->hal->close != NULL) {
        udp->hal->close();
    }
    udp->bound = false;
    memset(udp, 0, sizeof(transport_udp_t));
    return HAL_OK;
}

void transport_udp_set_hal_ops(transport_udp_t* udp, const transport_udp_hal_ops_t* ops)
{
    if (udp != NULL) {
        udp->hal = (ops != NULL) ? ops : &s_stub_hal;
    }
}

/* ---- Service Step ----
 *
 * RX: HAL recvfrom → rx_buffer
 * TX: tx_buffer → HAL sendto (partial write safe)
 */

void transport_udp_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    (void)timestamp_us;
    transport_udp_t* udp = comp_to_udp(comp);
    if (udp == NULL || udp->hal == NULL || !udp->bound) {
        return;
    }

    /* RX: recvfrom HAL into rx_buffer */
    if (udp->hal->recvfrom != NULL) {
        uint8_t rx_tmp[128];
        uint32_t src_ip = 0;
        uint16_t src_port = 0;
        int n = udp->hal->recvfrom(rx_tmp, sizeof(rx_tmp), &src_ip, &src_port);
        if (n > 0) {
            udp->rx_total += (uint32_t)n;
            udp->packets_rx++;
            byte_ring_buffer_write(&udp->rx_buffer, rx_tmp, (size_t)n);

            uint32_t overflows = byte_ring_buffer_overflow_count(&udp->rx_buffer);
            if (overflows > udp->rx_overflows) {
                udp->rx_overflows = overflows;
            }
        }
    }

    /* TX: drain tx_buffer → HAL sendto (partial write safe) */
    if (udp->hal->sendto != NULL) {
        uint8_t tx_tmp[128];
        while (byte_ring_buffer_available(&udp->tx_buffer) > 0) {
            size_t avail = byte_ring_buffer_available(&udp->tx_buffer);
            size_t chunk = avail > sizeof(tx_tmp) ? sizeof(tx_tmp) : avail;
            size_t drained = byte_ring_buffer_read(&udp->tx_buffer, tx_tmp, chunk);
            if (drained == 0) break;

            int written = udp->hal->sendto(tx_tmp, drained,
                                            udp->remote_ip, udp->remote_port);

            if (written <= 0) {
                /* Push all back */
                byte_ring_buffer_write(&udp->tx_buffer, tx_tmp, drained);
                udp->tx_backpressure++;
                break;
            }

            udp->tx_total += (uint32_t)written;
            udp->packets_tx++;

            if ((size_t)written < drained) {
                /* Partial write — push unwritten bytes back */
                byte_ring_buffer_write(&udp->tx_buffer,
                                       tx_tmp + written,
                                       drained - (size_t)written);
                udp->tx_backpressure++;
                break;
            }
        }
    }
}

/* ---- Public API ---- */

size_t transport_udp_rx_read(transport_udp_t* udp, uint8_t* buf, size_t max_len)
{
    if (udp == NULL || buf == NULL || max_len == 0) {
        return 0;
    }
    return byte_ring_buffer_read(&udp->rx_buffer, buf, max_len);
}

size_t transport_udp_tx_write(transport_udp_t* udp, const uint8_t* data, size_t len)
{
    if (udp == NULL || data == NULL || len == 0) {
        return 0;
    }
    return byte_ring_buffer_write(&udp->tx_buffer, data, len);
}

size_t transport_udp_rx_available(const transport_udp_t* udp)
{
    if (udp == NULL) {
        return 0;
    }
    return byte_ring_buffer_available(&udp->rx_buffer);
}

size_t transport_udp_tx_free(const transport_udp_t* udp)
{
    if (udp == NULL) {
        return 0;
    }
    if (udp->tx_buffer.capacity <= udp->tx_buffer.size) {
        return 0;
    }
    return udp->tx_buffer.capacity - udp->tx_buffer.size;
}

bool transport_udp_is_bound(const transport_udp_t* udp)
{
    if (udp == NULL) {
        return false;
    }
    return udp->bound;
}

/* ---- Diagnostics ---- */

hal_err_t transport_udp_get_diagnostics(const transport_udp_t* udp, transport_udp_diagnostics_t* diag)
{
    if (udp == NULL || diag == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    diag->rx_total          = udp->rx_total;
    diag->tx_total          = udp->tx_total;
    diag->rx_overflows      = udp->rx_overflows;
    diag->tx_backpressure   = udp->tx_backpressure;
    diag->packets_rx        = udp->packets_rx;
    diag->packets_tx        = udp->packets_tx;
    diag->rx_available      = byte_ring_buffer_available(&udp->rx_buffer);
    diag->tx_free           = (udp->tx_buffer.capacity >= udp->tx_buffer.size)
                                ? (udp->tx_buffer.capacity - udp->tx_buffer.size)
                                : 0;

    return HAL_OK;
}

/* ---- Reset ---- */

hal_err_t transport_udp_reset(transport_udp_t* udp)
{
    if (udp == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    byte_ring_buffer_init(&udp->rx_buffer, udp->rx_storage, sizeof(udp->rx_storage));
    byte_ring_buffer_init(&udp->tx_buffer, udp->tx_storage, sizeof(udp->tx_storage));

    udp->rx_total          = 0;
    udp->tx_total          = 0;
    udp->rx_overflows      = 0;
    udp->tx_backpressure   = 0;
    udp->packets_rx        = 0;
    udp->packets_tx        = 0;

    /* local_port, remote_ip, remote_port, hal, bound preserved */
    return HAL_OK;
}
