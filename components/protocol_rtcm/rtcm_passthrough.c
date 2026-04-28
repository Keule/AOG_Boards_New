#include "rtcm_passthrough.h"
#include <string.h>

void rtcm_passthrough_init(rtcm_passthrough_t* rtcm)
{
    if (rtcm == NULL) {
        return;
    }
    memset(rtcm, 0, sizeof(rtcm_passthrough_t));
}

void rtcm_passthrough_record_in(rtcm_passthrough_t* rtcm, size_t length, uint64_t timestamp_us)
{
    if (rtcm == NULL) {
        return;
    }
    rtcm->stats.bytes_in += (uint32_t)length;
    rtcm->stats.last_activity_us = timestamp_us;
}

void rtcm_passthrough_record_out(rtcm_passthrough_t* rtcm, size_t length)
{
    if (rtcm == NULL) {
        return;
    }
    rtcm->stats.bytes_out += (uint32_t)length;
}

void rtcm_passthrough_record_dropped(rtcm_passthrough_t* rtcm, size_t length)
{
    if (rtcm == NULL) {
        return;
    }
    rtcm->stats.bytes_dropped += (uint32_t)length;
}

void rtcm_passthrough_update_activity(rtcm_passthrough_t* rtcm, uint64_t timestamp_us)
{
    if (rtcm == NULL) {
        return;
    }
    rtcm->stats.last_activity_us = timestamp_us;
}

const rtcm_stats_t* rtcm_passthrough_get_stats(const rtcm_passthrough_t* rtcm)
{
    if (rtcm == NULL) {
        return NULL;
    }
    return &rtcm->stats;
}
