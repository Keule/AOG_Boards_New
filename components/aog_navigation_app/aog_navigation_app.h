#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gnss_dual_heading.h"
#include "gnss_um980.h"
#include "message_queue.h"
#include "runtime_component.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t data[64];
    uint16_t length;
} aog_frame_t;

typedef struct {
    const gnss_um980_t* primary;
    const gnss_dual_heading_t* heading;
    message_queue_t rx_queue;
    message_queue_t tx_queue;
    aog_frame_t rx_storage[8];
    aog_frame_t tx_storage[8];
    runtime_component_t component;
} aog_navigation_app_t;

void aog_navigation_app_init(aog_navigation_app_t* app, const gnss_um980_t* primary, const gnss_dual_heading_t* heading);
bool aog_navigation_app_feed_rx(aog_navigation_app_t* app, const aog_frame_t* frame);
bool aog_navigation_app_pop_tx(aog_navigation_app_t* app, aog_frame_t* out_frame);
void aog_navigation_app_step(aog_navigation_app_t* app);
runtime_component_t* aog_navigation_app_component(aog_navigation_app_t* app);

#ifdef __cplusplus
}
#endif
