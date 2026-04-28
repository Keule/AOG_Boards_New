#include "transport_udp.h"
#include <string.h>

static transport_udp_t* comp_to_udp(runtime_component_t* comp)
{
    return (transport_udp_t*)comp;
}

hal_err_t transport_udp_init(transport_udp_t* udp, const transport_udp_config_t* config)
{
    if (udp == NULL || config == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }

    memset(udp, 0, sizeof(transport_udp_t));
    udp->local_port  = config->local_port;
    udp->remote_ip   = config->remote_ip;
    udp->remote_port = config->remote_port;
    udp->bound       = true;

    byte_ring_buffer_init(&udp->rx_buffer, udp->rx_storage, sizeof(udp->rx_storage));
    byte_ring_buffer_init(&udp->tx_buffer, udp->tx_storage, sizeof(udp->tx_buffer));

    /* Register service step callback */
    udp->component.service_step = transport_udp_service_step;

    /* Stub: real implementation uses lwip/sockets to bind */
    return HAL_OK;
}

hal_err_t transport_udp_deinit(transport_udp_t* udp)
{
    if (udp == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    udp->bound = false;
    memset(udp, 0, sizeof(transport_udp_t));
    return HAL_OK;
}

void transport_udp_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    (void)timestamp_us;
    transport_udp_t* udp = comp_to_udp(comp);
    if (udp == NULL || !udp->bound) {
        return;
    }

    /* RX: stub - real implementation uses lwip recvfrom */
    /* Data would go into rx_buffer here */

    /* TX: drain tx_buffer → stub (real implementation uses lwip sendto) */
    uint8_t tx_tmp[64];
    size_t avail = byte_ring_buffer_available(&udp->tx_buffer);
    if (avail > 0) {
        size_t to_drain = avail > sizeof(tx_tmp) ? sizeof(tx_tmp) : avail;
        byte_ring_buffer_read(&udp->tx_buffer, tx_tmp, to_drain);
        /* Stub: send tx_tmp via lwip to udp->remote_ip:udp->remote_port */
        (void)tx_tmp;
    }
}

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
