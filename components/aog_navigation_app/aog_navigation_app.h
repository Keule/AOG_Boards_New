#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime_component.h"
#include "protocol_aog.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AOG_NAV_TX_DISCOVERY = 0,
    AOG_NAV_TX_POSITION,
    AOG_NAV_TX_HEADING
} aog_navigation_tx_kind_t;

typedef struct {
    aog_navigation_tx_kind_t kind;
    uint8_t payload[256];
    size_t length;
} aog_navigation_tx_item_t;

int aog_navigation_app_init(void);
runtime_component_t* aog_navigation_app_component(void);
bool aog_navigation_app_push_rx_frame(const aog_frame_t* frame);
bool aog_navigation_app_pop_tx(aog_navigation_tx_item_t* out_item);

#ifdef __cplusplus
}
#endif
