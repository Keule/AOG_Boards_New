/* ========================================================================
 * transport_udp.c — UDP Transport via lwip Sockets (NAV-ETH-BRINGUP-001)
 *
 * Replaces previous stub with real lwip socket I/O:
 *   - Non-blocking recvfrom() → rx_buffer (Core 0 service loop)
 *   - Non-blocking sendto() ← tx_buffer (Core 0 service loop)
 *   - Socket created on first service_step after IP available
 *   - No blocking in 100 Hz fast path — all socket I/O in service_step
 * ======================================================================== */

#include "transport_udp.h"
#include <string.h>

#if !defined(UNIT_TEST) && !defined(NATIVE_TEST)

/* ---- Real lwip socket implementation (ESP32 hardware builds) ---- */

#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/netdb.h"
#include "lwip/errno.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "TRANSPORT_UDP";

static transport_udp_t* comp_to_udp(runtime_component_t* comp)
{
    return (transport_udp_t*)comp;
}

/* ---- Helper: check if we have an IP address ---- */
static bool udp_has_ip(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (netif == NULL) return false;

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) return false;

    return ip_info.ip.addr != 0;
}

/* ---- Helper: open socket, bind, connect ---- */
static bool udp_open_socket(transport_udp_t* udp)
{
    if (udp->sock_fd >= 0) return true;  /* already open */

    if (!udp_has_ip()) {
        udp->status.has_ip = false;
        return false;
    }
    udp->status.has_ip = true;

    /* Create UDP socket */
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "UDP_ERROR: socket failed (errno=%d)", errno);
        udp->status.last_error = errno;
        udp->status.socket_open = false;
        return false;
    }

    /* Non-blocking */
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    /* Bind to local port */
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(udp->local_port);
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        ESP_LOGE(TAG, "UDP_ERROR: bind failed port=%d (errno=%d)",
                 udp->local_port, errno);
        udp->status.last_error = errno;
        close(sock);
        udp->status.socket_open = false;
        return false;
    }

    /* Connect to remote endpoint (sets default destination for sendto) */
    struct sockaddr_in remote_addr;
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(udp->remote_port);

    /* Convert host-order IP to network byte order.
     * remote_ip=0xFFFFFFFF means broadcast (255.255.255.255). */
    if (udp->remote_ip == 0xFFFFFFFF) {
        remote_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        /* Enable broadcast on socket */
        int broadcast_enable = 1;
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));
    } else {
        remote_addr.sin_addr.s_addr = htonl(udp->remote_ip);
    }

    /* Use connect() so we can send() without specifying address each time */
    if (connect(sock, (struct sockaddr*)&remote_addr, sizeof(remote_addr)) < 0) {
        ESP_LOGE(TAG, "UDP_ERROR: connect failed (errno=%d)", errno);
        udp->status.last_error = errno;
        close(sock);
        udp->status.socket_open = false;
        return false;
    }

    udp->sock_fd = sock;
    udp->status.socket_open = true;
    udp->status.bound = true;
    udp->status.last_error = 0;

    ESP_LOGI(TAG, "UDP: rx_socket=ok local_port=%d", udp->local_port);

    /* Log remote target (human-readable) */
    uint8_t *ip_bytes = (uint8_t*)&udp->remote_ip;
    if (udp->remote_ip == 0xFFFFFFFF) {
        ESP_LOGI(TAG, "UDP: tx_socket=ok target=255.255.255.255:%d (broadcast)",
                 udp->remote_port);
    } else {
        ESP_LOGI(TAG, "UDP: tx_socket=ok target=%u.%u.%u.%u:%d",
                 ip_bytes[3], ip_bytes[2], ip_bytes[1], ip_bytes[0],
                 udp->remote_port);
    }

    return true;
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
    udp->sock_fd     = -1;

    byte_ring_buffer_init(&udp->rx_buffer, udp->rx_storage, sizeof(udp->rx_storage));
    byte_ring_buffer_init(&udp->tx_buffer, udp->tx_storage, sizeof(udp->tx_buffer));

    /* Register service step callback */
    udp->component.service_step = transport_udp_service_step;

    ESP_LOGI(TAG, "UDP: init local_port=%d (waiting_for_ip)", udp->local_port);

    return HAL_OK;
}

hal_err_t transport_udp_deinit(transport_udp_t* udp)
{
    if (udp == NULL) {
        return HAL_ERR_INVALID_PARAM;
    }
    if (udp->sock_fd >= 0) {
        close(udp->sock_fd);
        udp->sock_fd = -1;
    }
    memset(udp, 0, sizeof(transport_udp_t));
    udp->sock_fd = -1;
    return HAL_OK;
}

void transport_udp_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    (void)timestamp_us;
    transport_udp_t* udp = comp_to_udp(comp);
    if (udp == NULL) return;

    /* ---- Lazy socket open: wait for IP first ---- */
    if (udp->sock_fd < 0) {
        if (!udp_open_socket(udp)) {
            /* Will retry next service_step */
            return;
        }
    }

    /* ---- RX: non-blocking recvfrom → rx_buffer ---- */
    {
        uint8_t rx_tmp[128];
        ssize_t n = recv(udp->sock_fd, rx_tmp, sizeof(rx_tmp), MSG_DONTWAIT);
        if (n > 0) {
            size_t written = byte_ring_buffer_write(&udp->rx_buffer, rx_tmp, (size_t)n);
            if (written < (size_t)n) {
                /* rx_buffer full — discard overflow */
                udp->status.rx_errors++;
            }
            udp->status.rx_packets++;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            udp->status.rx_errors++;
            udp->status.last_error = errno;
        }
    }

    /* ---- TX: tx_buffer → non-blocking send ---- */
    {
        uint8_t tx_tmp[128];
        size_t avail = byte_ring_buffer_available(&udp->tx_buffer);
        while (avail > 0) {
            size_t to_send = avail > sizeof(tx_tmp) ? sizeof(tx_tmp) : avail;
            size_t to_drain = to_send;
            byte_ring_buffer_peek(&udp->tx_buffer, tx_tmp, to_send);

            ssize_t n = send(udp->sock_fd, tx_tmp, to_send, MSG_DONTWAIT);
            if (n > 0) {
                /* Success — drain sent bytes from ring buffer */
                byte_ring_buffer_read(&udp->tx_buffer, tx_tmp, (size_t)n);
                udp->status.tx_packets++;
                avail = byte_ring_buffer_available(&udp->tx_buffer);
            } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                /* Socket buffer full — try again next cycle */
                break;
            } else if (n < 0) {
                /* Real error */
                udp->status.tx_errors++;
                udp->status.last_error = errno;
                /* Drain to avoid buffer deadlock */
                byte_ring_buffer_read(&udp->tx_buffer, tx_tmp, to_drain);
                break;
            } else {
                /* n == 0 shouldn't happen for DGRAM */
                byte_ring_buffer_read(&udp->tx_buffer, tx_tmp, to_drain);
                break;
            }
        }
    }
}

#else /* UNIT_TEST or NATIVE_TEST — keep stub implementation */

#include "esp_log.h"
static const char* TAG = "TRANSPORT_UDP";

static transport_udp_t* comp_to_udp(runtime_component_t* comp)
{
    return (transport_udp_t*)comp;
}

hal_err_t transport_udp_init(transport_udp_t* udp, const transport_udp_config_t* config)
{
    if (udp == NULL || config == NULL) return HAL_ERR_INVALID_PARAM;
    memset(udp, 0, sizeof(transport_udp_t));
    udp->local_port  = config->local_port;
    udp->remote_ip   = config->remote_ip;
    udp->remote_port = config->remote_port;
    udp->sock_fd     = -1;
    byte_ring_buffer_init(&udp->rx_buffer, udp->rx_storage, sizeof(udp->rx_storage));
    byte_ring_buffer_init(&udp->tx_buffer, udp->tx_storage, sizeof(udp->tx_buffer));
    udp->component.service_step = transport_udp_service_step;
    return HAL_OK;
}

hal_err_t transport_udp_deinit(transport_udp_t* udp)
{
    if (udp == NULL) return HAL_ERR_INVALID_PARAM;
    memset(udp, 0, sizeof(transport_udp_t));
    udp->sock_fd = -1;
    return HAL_OK;
}

void transport_udp_service_step(runtime_component_t* comp, uint64_t timestamp_us)
{
    (void)comp; (void)timestamp_us;
    /* Stub: no-op for host tests */
}

#endif /* UNIT_TEST / NATIVE_TEST */

/* ---- Buffer API (shared between real and stub) ---- */

size_t transport_udp_rx_read(transport_udp_t* udp, uint8_t* buf, size_t max_len)
{
    if (udp == NULL || buf == NULL || max_len == 0) return 0;
    return byte_ring_buffer_read(&udp->rx_buffer, buf, max_len);
}

size_t transport_udp_tx_write(transport_udp_t* udp, const uint8_t* data, size_t len)
{
    if (udp == NULL || data == NULL || len == 0) return 0;
    return byte_ring_buffer_write(&udp->tx_buffer, data, len);
}

size_t transport_udp_rx_available(const transport_udp_t* udp)
{
    if (udp == NULL) return 0;
    return byte_ring_buffer_available(&udp->rx_buffer);
}

size_t transport_udp_tx_free(const transport_udp_t* udp)
{
    if (udp == NULL) return 0;
    if (udp->tx_buffer.capacity <= udp->tx_buffer.size) return 0;
    return udp->tx_buffer.capacity - udp->tx_buffer.size;
}

bool transport_udp_is_bound(const transport_udp_t* udp)
{
    if (udp == NULL) return false;
    return udp->status.bound;
}

const transport_udp_status_t* transport_udp_get_status(const transport_udp_t* udp)
{
    if (udp == NULL) return NULL;
    return &udp->status;
}
