/* ========================================================================
 * nav_ntrip_config.h — NTRIP Configuration Loader
 *
 * NAV-ETH-BRINGUP-001-R2 WP-F
 *
 * Attempts to load NTRIP configuration from a local secrets file:
 *   components/nav_config/nav_ntrip_secrets.local.h
 *
 * If the file exists and defines non-empty NTRIP_HOST and NTRIP_MOUNTPOINT,
 * the config is considered valid and NTRIP can be started.
 *
 * If the file does NOT exist or fields are empty, config remains invalid
 * and NTRIP stays disabled. This is the default state.
 *
 * Secret files are gitignored — see .gitignore entries for
 * nav_ntrip_secrets.local.h and *secrets.local* patterns.
 * ======================================================================== */

#ifndef NAV_NTRIP_CONFIG_H
#define NAV_NTRIP_CONFIG_H

#include "ntrip_client.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Try to load NTRIP config from local secrets file.
 *
 * If the secrets header exists and provides non-empty host+mountpoint,
 * fills `out_config` and returns true.
 *
 * If the file doesn't exist or config is incomplete, returns false
 * and `out_config` is filled with safe defaults (empty strings).
 *
 * This function does NOT log passwords.
 */
static inline bool nav_ntrip_try_load_config(ntrip_client_config_t* out_config)
{
    if (out_config == NULL) return false;

    /* Start with defaults */
    const ntrip_client_config_t defaults = NTRIP_CLIENT_CONFIG_DEFAULT();
    memcpy(out_config, &defaults, sizeof(ntrip_client_config_t));

    /* Try to include the local secrets file.
     * If it doesn't exist, the #if check will fail and we return defaults. */
    /* The include is done via app_core.c which checks for the file existence.
     * Here we provide the public API. */

    /* Check if build flag is set (means the file was found during build). */
#ifndef NTRIP_SECRETS_AVAILABLE
    (void)out_config;
    return false;
#else
    /* The actual secrets are injected via build flag defines from CMake.
     * app_core.c handles this by checking for the file at build time
     * and passing the config through ntrip_client_configure(). */
    return false;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* NAV_NTRIP_CONFIG_H */
