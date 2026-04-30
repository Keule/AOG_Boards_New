#include "runtime_mode.h"
#include <string.h>

/* =========================================================================
 * Work / Config service profiles (pure data, no ESP dependency)
 *
 * Work:   UART/UDP/TCP_NTRIP aktiv, Diagnostics langsam / niedrig prio.
 * Config: UART/UDP/TCP_NTRIP aktiv, Diagnostics schneller / hoeher prio.
 * ========================================================================= */

static const service_profile_t s_work_profiles[SERVICE_GROUP_COUNT] = {
    [SERVICE_GROUP_UART]       = { .priority = 5, .period_ms = 10, .suspended = false },
    [SERVICE_GROUP_UDP]        = { .priority = 5, .period_ms = 10, .suspended = false },
    [SERVICE_GROUP_TCP_NTRIP]  = { .priority = 5, .period_ms = 10, .suspended = false },
    [SERVICE_GROUP_DIAGNOSTICS]= { .priority = 3, .period_ms = 100, .suspended = false },
};

static const service_profile_t s_config_profiles[SERVICE_GROUP_COUNT] = {
    [SERVICE_GROUP_UART]       = { .priority = 5, .period_ms = 10, .suspended = false },
    [SERVICE_GROUP_UDP]        = { .priority = 5, .period_ms = 10, .suspended = false },
    [SERVICE_GROUP_TCP_NTRIP]  = { .priority = 5, .period_ms = 10, .suspended = false },
    [SERVICE_GROUP_DIAGNOSTICS]= { .priority = 6, .period_ms = 50,  .suspended = false },
};

/* Live profile state */
static service_profile_t s_profiles[SERVICE_GROUP_COUNT];

/* Current system mode */
static system_mode_t s_current_mode = SYSTEM_MODE_WORK;

/* ---- Internal: copy profile set into live state ---- */

static void apply_profiles(const service_profile_t profiles[SERVICE_GROUP_COUNT])
{
    for (int i = 0; i < SERVICE_GROUP_COUNT; i++) {
        s_profiles[i] = profiles[i];
    }
}

/* =========================================================================
 * Public API (host-testable)
 * ========================================================================= */

void runtime_mode_init(void)
{
    s_current_mode = SYSTEM_MODE_WORK;
    apply_profiles(s_work_profiles);
}

int runtime_mode_set(system_mode_t mode)
{
    if (mode < 0 || mode >= SYSTEM_MODE_COUNT) {
        return -1;
    }

    if (mode == s_current_mode) {
        return 0;
    }

    apply_profiles((mode == SYSTEM_MODE_WORK)
        ? s_work_profiles
        : s_config_profiles);

    s_current_mode = mode;
    return 0;
}

system_mode_t runtime_mode_get(void)
{
    return s_current_mode;
}

const service_profile_t* runtime_mode_get_profile(service_group_t group)
{
    if (group < 0 || group >= SERVICE_GROUP_COUNT) {
        return NULL;
    }
    return &s_profiles[group];
}

const service_profile_t* runtime_mode_work_profile(service_group_t group)
{
    if (group < 0 || group >= SERVICE_GROUP_COUNT) {
        return NULL;
    }
    return &s_work_profiles[group];
}

const service_profile_t* runtime_mode_config_profile(service_group_t group)
{
    if (group < 0 || group >= SERVICE_GROUP_COUNT) {
        return NULL;
    }
    return &s_config_profiles[group];
}
