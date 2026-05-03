#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hal_backend.h"
#include "board_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- HAL Ethernet Ops ---- */

typedef struct {
    hal_err_t (*init)(void);
    hal_err_t (*deinit)(void);
    hal_err_t (*get_mac)(uint8_t mac[6]);
    bool (*is_connected)(void);
} hal_eth_ops_t;

/* ---- HAL Ethernet Extended Status ---- */

typedef struct {
    bool     initialized;      /* true after successful esp_eth_start */
    bool     link_up;          /* true when PHY reports link */
    bool     got_ip;           /* true when DHCP/static IP assigned */
    uint8_t  ip_addr[4];       /* {0,0,0,0} until DHCP assigns */
    uint8_t  netmask[4];
    uint8_t  gw[4];
} hal_eth_status_t;

/* ---- HAL Ethernet API ---- */

hal_err_t hal_eth_init(const hal_eth_ops_t* ops);
hal_err_t hal_eth_deinit(void);
hal_err_t hal_eth_get_mac(uint8_t mac[6]);
bool hal_eth_is_connected(void);
const hal_eth_ops_t* hal_eth_esp32_ops(void);

/* Get ethernet controller kind from board profile. */
ethernet_kind_t hal_eth_get_controller(void);

/* Get real-time ETH status (link, IP, etc.) */
const hal_eth_status_t* hal_eth_get_status(void);

#ifdef __cplusplus
}
#endif
