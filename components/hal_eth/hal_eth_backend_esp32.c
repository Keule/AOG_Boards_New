#include "hal_eth_backend.h"

int hal_eth_backend_esp32_init(ethernet_kind_t kind)
{
    if (kind == ETH_UNKNOWN) {
        return -1;
    }

    return 0;
}

int hal_eth_backend_esp32_deinit(void)
{
    return 0;
}
