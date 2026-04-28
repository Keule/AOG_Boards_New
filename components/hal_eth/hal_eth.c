#include "hal_eth.h"

#include "hal_backend.h"
#include "hal_eth_backend.h"

static ethernet_kind_t s_eth_kind = ETH_UNKNOWN;

int hal_eth_init(void)
{
    s_eth_kind = board_profile_get_eth();

    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        return hal_eth_backend_sim_init(s_eth_kind);
    }

    return hal_eth_backend_esp32_init(s_eth_kind);
}

int hal_eth_deinit(void)
{
    int rc = 0;

    if (hal_backend_get_kind() == HAL_BACKEND_SIM) {
        rc = hal_eth_backend_sim_deinit();
    } else {
        rc = hal_eth_backend_esp32_deinit();
    }

    s_eth_kind = ETH_UNKNOWN;
    return rc;
}

ethernet_kind_t hal_eth_kind(void)
{
    return s_eth_kind;
}
