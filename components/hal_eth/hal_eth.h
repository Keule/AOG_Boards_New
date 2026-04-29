#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hal_backend.h"
#include "board_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- HAL Ethernet Status ---- */

typedef struct {
    bool     link_up;        /* Physical link detected */
    bool     ip_acquired;    /* DHCP or static IP configured */
    uint32_t ip_addr;        /* IPv4 address in host byte order (0 = none) */
    uint32_t netmask;        /* Subnet mask in host byte order */
    uint32_t gateway;        /* Gateway in host byte order */
    uint8_t  mac[6];         /* MAC address */
    uint32_t error_flags;    /* Bitfield: 1=init_failed, 2=link_lost, 4=dhcp_failed */
} hal_eth_status_t;

/* Error flag bits for hal_eth_status_t.error_flags */
#define HAL_ETH_ERR_INIT_FAILED   (1u << 0)
#define HAL_ETH_ERR_LINK_LOST     (1u << 1)
#define HAL_ETH_ERR_DHCP_FAILED   (1u << 2)
#define HAL_ETH_ERR_SPI_FAILED    (1u << 3)

/* ---- HAL Ethernet Ops ---- */

typedef struct {
    hal_err_t (*init)(void);
    hal_err_t (*deinit)(void);
    hal_err_t (*get_status)(hal_eth_status_t* status);
} hal_eth_ops_t;

/* ---- HAL Ethernet API ---- */

hal_err_t hal_eth_init(const hal_eth_ops_t* ops);
hal_err_t hal_eth_deinit(void);

/* Get current ethernet status (link, IP, MAC, errors). Non-blocking. */
hal_err_t hal_eth_get_status(hal_eth_status_t* status);

/* Convenience: check link + IP in one call. */
bool hal_eth_is_connected(void);

/* Get ethernet controller kind from board profile. */
ethernet_kind_t hal_eth_get_controller(void);

/* Get the ESP32 HAL ops struct (stubs on host, productive on ESP32). */
const hal_eth_ops_t* hal_eth_esp32_ops(void);

#ifdef __cplusplus
}
#endif
