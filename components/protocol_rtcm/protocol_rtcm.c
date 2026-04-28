#include "protocol_rtcm.h"

#include <string.h>

void rtcm_passthrough_init(rtcm_counters_t* counters)
{
    if (counters == NULL) {
        return;
    }

    counters->bytes_in = 0;
    counters->bytes_out = 0;
    counters->dropped = 0;
    counters->last_activity = 0;
}

size_t rtcm_passthrough_process(
    rtcm_counters_t* counters,
    const uint8_t* in_data,
    size_t in_size,
    uint8_t* out_data,
    size_t out_capacity,
    uint64_t now_us
)
{
    size_t out_size = 0;

    if (counters == NULL) {
        return 0;
    }

    counters->bytes_in += in_size;

    if (in_data == NULL || out_data == NULL || out_capacity == 0) {
        counters->dropped += in_size;
        if (in_size > 0) {
            counters->last_activity = now_us;
        }
        return 0;
    }

    out_size = (in_size <= out_capacity) ? in_size : out_capacity;
    memcpy(out_data, in_data, out_size);

    counters->bytes_out += out_size;
    counters->dropped += (in_size - out_size);

    if (in_size > 0) {
        counters->last_activity = now_us;
    }

    return out_size;
}
