#include "transport_tcp.h"
#include <string.h>

static transport_tcp_t* comp_to_tcp(runtime_component_t* comp)
{
    return (transport_tcp_t*)comp;
}

hal_err_t transport_tcp_init(transport_tcp_t* tcp, const transport_tcp_config_t* config)
{
    if (tcp == NULL || config == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    memset(tcp, 0, sizeof(transport_tcp_t));
    tcp->remote_ip   = config->remote_ip;
    tcp->remote_port = config->remote_port;
    tcp->connected   = false;

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

hal_err_t transport_tcp_connect(transport_tcp_t* tcp)
{
    if (tcp == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    /* Stub: real implementation uses lwip tcp_connect */
    tcp->connected = true;
    return HAL_OK;
}

hal_err_t transport_tcp_disconnect(transport_tcp_t* tcp)
{
    if (tcp == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    tcp->connected = false;
    /* Clear TX buffer — unsent data is discarded on disconnect */
    tcp->tx_buffer.head = 0;
    tcp->tx_buffer.tail = 0;
    tcp->tx_buffer.size = 0;
    return HAL_OK;
}

void transport_tcp_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    (void)timestamp_us;
    transport_tcp_t* tcp = comp_to_tcp(comp);
    if (tcp == NULL || !tcp->connected) {
        return;
    }

    /* TX: drain tx_buffer → HAL TCP (stub: just discard) */
    {
        size_t avail = byte_ring_buffer_available(&tcp->tx_buffer);
        if (avail > 0) {
            uint8_t tmp[64];
            size_t to_drain = avail > sizeof(tmp) ? sizeof(tmp) : avail;
            byte_ring_buffer_read(&tcp->tx_buffer, tmp, to_drain);
            /* Real implementation: lwip tcp_write() */
        }
    }

    /* RX: stub - real implementation uses lwip recv */
    /* Data would go into rx_buffer here */
}

size_t transport_tcp_tx_write(transport_tcp_t* tcp, const uint8_t* data, size_t len)
{
    if (tcp == NULL || data == NULL || len == 0) {
        return 0;
    }
    if (!tcp->connected) {
        return 0;
    }
    return byte_ring_buffer_write(&tcp->tx_buffer, data, len);
}

size_t transport_tcp_rx_read(transport_tcp_t* tcp, uint8_t* buf, size_t max_len)
{
    if (tcp == NULL || buf == NULL || max_len == 0) {
        return 0;
    }
    return byte_ring_buffer_read(&tcp->rx_buffer, buf, max_len);
}

size_t transport_tcp_rx_available(const transport_tcp_t* tcp)
{
    if (tcp == NULL) {
        return 0;
    }
    return byte_ring_buffer_available(&tcp->rx_buffer);
}

size_t transport_tcp_tx_available(const transport_tcp_t* tcp)
{
    if (tcp == NULL) {
        return 0;
    }
    return byte_ring_buffer_available(&tcp->tx_buffer);
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
