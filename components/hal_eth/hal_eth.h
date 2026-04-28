#pragma once

#include "board_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

int hal_eth_init(void);
int hal_eth_deinit(void);
ethernet_kind_t hal_eth_kind(void);

#ifdef __cplusplus
}
#endif
