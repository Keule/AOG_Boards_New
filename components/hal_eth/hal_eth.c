#include "hal_eth.h"

static ethernet_kind_t s_eth_kind = ETH_UNKNOWN;

int hal_eth_init(void)
{
    s_eth_kind = board_profile_get_eth();

    if (s_eth_kind == ETH_UNKNOWN) {
        return -1;
    }

    return 0;
}

int hal_eth_deinit(void)
{
    s_eth_kind = ETH_UNKNOWN;
    return 0;
}

ethernet_kind_t hal_eth_kind(void)
{
    return s_eth_kind;
}
