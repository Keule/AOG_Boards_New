#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime_component.h"
#include "hal_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_uart_port_t port;
    uint8_t payload[512];
    size_t length;
} rtcm_router_tx_item_t;

int rtcm_router_init(void);
runtime_component_t* rtcm_router_component(void);
void rtcm_router_push_from_ntrip(const uint8_t* data, size_t length, uint64_t now_us);
bool rtcm_router_pop_uart_tx(rtcm_router_tx_item_t* out_item);

#ifdef __cplusplus
}
#endif
