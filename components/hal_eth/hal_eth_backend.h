#pragma once

#include "board_profile.h"

int hal_eth_backend_esp32_init(ethernet_kind_t kind);
int hal_eth_backend_esp32_deinit(void);
int hal_eth_backend_sim_init(ethernet_kind_t kind);
int hal_eth_backend_sim_deinit(void);
