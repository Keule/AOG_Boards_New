#include "esp_log.h"
#include "feature_flags.h"

static const char* TAG = "FEATURE_FLAGS";

unsigned int feature_flags_get(void)
{
    unsigned int flags = 0;

#if defined(DEVICE_ROLE_NAVIGATION)
    flags |= FEATURE_ROLE_NAVIGATION;
#endif

#if defined(DEVICE_ROLE_STEERING)
    flags |= FEATURE_ROLE_STEERING;
#endif

#if defined(DEVICE_ROLE_FULL_TEST)
    flags |= FEATURE_ROLE_FULL_TEST;
#endif

    ESP_LOGI(TAG, "Feature flags: 0x%02x", flags);
    return flags;
}
