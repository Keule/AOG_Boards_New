#include "transport_tcp.h"
#include <string.h>

/* ---- Internal helpers ---- */

static transport_tcp_t* comp_to_tcp(runtime_component_t* comp)
{
    return (transport_tcp_t*)comp;
}

/* Stub HAL ops — do nothing, return success */
static hal_err_t stub_connect(uint32_t ip, uint16_t port)  { (void)ip; (void)port; return HAL_OK; }
static hal_err_t stub_disconnect(void)                      { return HAL_OK; }
static int stub_recv(uint8_t* buf, size_t len)             { (void)buf; (void)len; return 0; }
static int stub_send(const uint8_t* buf, size_t len)       { (void)buf; (void)len; return 0; }
static bool stub_is_connected(void)                         { return false; }

static const transport_tcp_hal_ops_t s_stub_hal = {
    .connect       = stub_connect,
    .disconnect    = stub_disconnect,
    .recv          = stub_recv,
    .send          = stub_send,
    .is_connected  = stub_is_connected,
};

/* ---- Init / Deinit ---- */

hal_err_t transport_tcp_init(transport_tcp_t* tcp, const transport_tcp_config_t* config)
{
    if (tcp == NULL || config == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    memset(tcp, 0, sizeof(transport_tcp_t));
    tcp->remote_ip   = config->remote_ip;
    tcp->remote_port = config->remote_port;
    tcp->connected   = false;
    tcp->hal         = &s_stub_hal;

    byte_ring_buffer_init(&tcp->rx_buffer, tcp->rx_storage, sizeof(tcp->rx_storage));
    byte_ring_buffer_init(&tcp->tx_buffer, tcp->tx_storage, sizeof(tcp->tx_storage));

    /* Register service step callback */
    tcp->component.service_step = transport_tcp_service_step;

    return HAL_OK;
}

hal_err_t transport_tcp_deinit(transport_tcp_t* tcp)
{
    if (tcp == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    transport_tcp_disconnect(tcp);
    memset(tcp, 0, sizeof(transport_tcp_t));
    return HAL_OK;
}

void transport_tcp_set_hal_ops(transport_tcp_t* tcp, const transport_tcp_hal_ops_t* ops)
{
    if (tcp != NULL) {
        tcp->hal = (ops != NULL) ? ops : &s_stub_hal;
    }
}

/* ---- Connect / Disconnect ---- */

hal_err_t transport_tcp_connect(transport_tcp_t* tcp)
{
    if (tcp == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    if (tcp->connected) {
        return HAL_OK;
    }

    hal_err_t err = HAL_OK;
    if (tcp->hal != NULL && tcp->hal->connect != NULL) {
        err = tcp->hal->connect(tcp->remote_ip, tcp->remote_port);
    }

    if (err == HAL_OK) {
        tcp->connected = true;
        tcp->connect_count++;
    }

    return err;
}

hal_err_t transport_tcp_disconnect(transport_tcp_t* tcp)
{
    if (tcp == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    if (!tcp->connected) {
        return HAL_OK;
    }

    if (tcp->hal != NULL && tcp->hal->disconnect != NULL) {
        tcp->hal->disconnect();
    }

    tcp->connected = false;
    tcp->disconnect_count++;
    return HAL_OK;
}

/* ---- Service Step ----
 *
 * RX: HAL recv → rx_buffer
 * TX: tx_buffer → HAL send (partial write safe)
 */

void transport_tcp_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    (void)timestamp_us;
    transport_tcp_t* tcp = comp_to_tcp(comp);
    if (tcp == NULL || tcp->hal == NULL) {
        return;
    }

    /* Check if HAL thinks we're still connected */
    if (tcp->connected && tcp->hal->is_connected != NULL) {
        if (!tcp->hal->is_connected()) {
            tcp->connected = false;
            tcp->disconnect_count++;
        }
    }

    /* RX: recv from HAL into rx_buffer */
    if (tcp->connected && tcp->hal->recv != NULL) {
        uint8_t rx_tmp[128];
        int n = tcp->hal->recv(rx_tmp, sizeof(rx_tmp));
        if (n > 0) {
            tcp->rx_total += (uint32_t)n;
            byte_ring_buffer_write(&tcp->rx_buffer, rx_tmp, (size_t)n);

            uint32_t overflows = byte_ring_buffer_overflow_count(&tcp->rx_buffer);
            if (overflows > tcp->rx_overflows) {
                tcp->rx_overflows = overflows;
            }
        }
    }

    /* TX: drain tx_buffer → HAL send (partial write safe) */
    if (tcp->connected && tcp->hal->send != NULL) {
        uint8_t tx_tmp[128];
        while (byte_ring_buffer_available(&tcp->tx_buffer) > 0) {
            size_t avail = byte_ring_buffer_available(&tcp->tx_buffer);
            size_t chunk = avail > sizeof(tx_tmp) ? sizeof(tx_tmp) : avail;
            size_t drained = byte_ring_buffer_read(&tcp->tx_buffer, tx_tmp, chunk);
            if (drained == 0) break;

            int written = tcp->hal->send(tx_tmp, drained);

            if (written <= 0) {
                /* Push all back */
                byte_ring_buffer_write(&tcp->tx_buffer, tx_tmp, drained);
                tcp->tx_backpressure++;
                break;
            }

            tcp->tx_total += (uint32_t)written;

            if ((size_t)written < drained) {
                /* Partial write — push unwritten bytes back */
                byte_ring_buffer_write(&tcp->tx_buffer,
                                       tx_tmp + written,
                                       drained - (size_t)written);
                tcp->tx_backpressure++;
                break;
            }
        }
    }
}

/* ---- Public API ---- */

size_t transport_tcp_rx_read(transport_tcp_t* tcp, uint8_t* buf, size_t max_len)
{
    if (tcp == NULL || buf == NULL || max_len == 0) {
        return 0;
    }
    return byte_ring_buffer_read(&tcp->rx_buffer, buf, max_len);
}

size_t transport_tcp_tx_write(transport_tcp_t* tcp, const uint8_t* data, size_t len)
{
    if (tcp == NULL || data == NULL || len == 0) {
        return 0;
    }
    return byte_ring_buffer_write(&tcp->tx_buffer, data, len);
}

size_t transport_tcp_rx_available(const transport_tcp_t* tcp)
{
    if (tcp == NULL) {
        return 0;
    }
    return byte_ring_buffer_available(&tcp->rx_buffer);
}

size_t transport_tcp_tx_free(const transport_tcp_t* tcp)
{
    if (tcp == NULL) {
        return 0;
    }
    if (tcp->tx_buffer.capacity <= tcp->tx_buffer.size) {
        return 0;
    }
    return tcp->tx_buffer.capacity - tcp->tx_buffer.size;
}

bool transport_tcp_is_connected(const transport_tcp_t* tcp)
{
    if (tcp == NULL) {
        return false;
    }
    return tcp->connected;
}

/* ---- Diagnostics ---- */

hal_err_t transport_tcp_get_diagnostics(const transport_tcp_t* tcp, transport_tcp_diagnostics_t* diag)
{
    if (tcp == NULL || diag == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    diag->rx_total          = tcp->rx_total;
    diag->tx_total          = tcp->tx_total;
    diag->rx_overflows      = tcp->rx_overflows;
    diag->tx_backpressure   = tcp->tx_backpressure;
    diag->connect_count     = tcp->connect_count;
    diag->disconnect_count  = tcp->disconnect_count;
    diag->rx_available      = byte_ring_buffer_available(&tcp->rx_buffer);
    diag->tx_free           = (tcp->tx_buffer.capacity >= tcp->tx_buffer.size)
                                ? (tcp->tx_buffer.capacity - tcp->tx_buffer.size)
                                : 0;

    return HAL_OK;
}

/* ---- Reset ---- */

hal_err_t transport_tcp_reset(transport_tcp_t* tcp)
{
    if (tcp == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    transport_tcp_disconnect(tcp);

    byte_ring_buffer_init(&tcp->rx_buffer, tcp->rx_storage, sizeof(tcp->rx_storage));
    byte_ring_buffer_init(&tcp->tx_buffer, tcp->tx_storage, sizeof(tcp->tx_storage));

    tcp->rx_total          = 0;
    tcp->tx_total          = 0;
    tcp->rx_overflows      = 0;
    tcp->tx_backpressure   = 0;
    tcp->connect_count     = 0;
    tcp->disconnect_count  = 0;

    /* remote_ip, remote_port, hal preserved */
    return HAL_OK;
}
