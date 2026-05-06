/* ========================================================================
 * nav_ntrip_config.h — NTRIP Configuration Loader
 *
 * R6-NTRIP-CFG-FIX: Complete rewrite
 *
 * TWO config sources (priority order):
 *   1. nav_ntrip_secrets.local.h  — gitignored file, detected by CMake
 *   2. Compile-time build flags   — -D NTRIP_HOST=\"...\" from platformio.ini
 *
 * If neither source provides non-empty host+mountpoint, config stays
 * invalid and NTRIP stays disabled (safe default).
 *
 * HARD RULES:
 *   - Password is NEVER logged in plaintext
 *   - No blocking operations
 *   - No NTRIP code in task_fast
 * ======================================================================== */

#ifndef NAV_NTRIP_CONFIG_H
#define NAV_NTRIP_CONFIG_H

#include "ntrip_client.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Config source tracking for diagnostics */
typedef enum {
    NTRIP_CFG_SRC_MISSING = 0,       /* No config found — NTRIP disabled */
    NTRIP_CFG_SRC_COMPILE_TIME,      /* From platformio.ini build flags */
    NTRIP_CFG_SRC_LOCAL_SECRETS,     /* From nav_ntrip_secrets.local.h */
    NTRIP_CFG_SRC_NVS_OVERRIDE,      /* Reserved: future NVS support */
} ntrip_config_source_t;

/* Source name for diagnostics (never secrets content) */
static inline const char* ntrip_config_source_name(ntrip_config_source_t src)
{
    switch (src) {
    case NTRIP_CFG_SRC_COMPILE_TIME:  return "compile_time_defaults";
    case NTRIP_CFG_SRC_LOCAL_SECRETS: return "local_secrets";
    case NTRIP_CFG_SRC_NVS_OVERRIDE:  return "nvs_override";
    case NTRIP_CFG_SRC_MISSING:
    default:                           return "missing";
    }
}

/**
 * Load NTRIP config from available sources.
 *
 * Priority: local_secrets > compile_time_build_flags > missing
 *
 * Returns the config source that was used.
 * If source is MISSING, out_config has safe defaults (empty host/mount).
 * Password is copied into out_config but MUST NOT be logged.
 */
static inline ntrip_config_source_t nav_ntrip_load_config(ntrip_client_config_t* out)
{
    if (out == NULL) return NTRIP_CFG_SRC_MISSING;

    /* Start with safe defaults */
    *out = (ntrip_client_config_t)NTRIP_CLIENT_CONFIG_DEFAULT();

    /* ---- Priority 1: Local secrets file (CMake-detected) ----
     * CMake sets NTRIP_SECRETS_AVAILABLE=1 when nav_ntrip_secrets.local.h exists.
     * The secrets file defines: NTRIP_HOST, NTRIP_PORT, NTRIP_MOUNTPOINT,
     *                           NTRIP_USER, NTRIP_PASSWORD */
#ifdef NTRIP_SECRETS_AVAILABLE
    #include "nav_ntrip_secrets.local.h"

    /* Verify the macros are actually defined and non-empty.
     * (The file exists but fields could still be placeholder-empty.) */
    #if defined(NTRIP_HOST) && defined(NTRIP_MOUNTPOINT)
    if (NTRIP_HOST[0] != '\0' && NTRIP_MOUNTPOINT[0] != '\0') {
        strncpy(out->host, NTRIP_HOST, NTRIP_MAX_HOST_LEN - 1);
        out->host[NTRIP_MAX_HOST_LEN - 1] = '\0';

        #ifdef NTRIP_PORT
        out->port = (uint16_t)NTRIP_PORT;
        #else
        out->port = 2101;  /* NTRIP default */
        #endif

        strncpy(out->mountpoint, NTRIP_MOUNTPOINT, NTRIP_MAX_MOUNTPOINT_LEN - 1);
        out->mountpoint[NTRIP_MAX_MOUNTPOINT_LEN - 1] = '\0';

        #ifdef NTRIP_USER
        strncpy(out->username, NTRIP_USER, NTRIP_MAX_CRED_LEN - 1);
        out->username[NTRIP_MAX_CRED_LEN - 1] = '\0';
        #endif

        #ifdef NTRIP_PASSWORD
        strncpy(out->password, NTRIP_PASSWORD, NTRIP_MAX_CRED_LEN - 1);
        out->password[NTRIP_MAX_CRED_LEN - 1] = '\0';
        #endif

        return NTRIP_CFG_SRC_LOCAL_SECRETS;
    }
    #endif
#endif /* NTRIP_SECRETS_AVAILABLE */

    /* ---- Priority 2: Compile-time build flags from platformio.ini ----
     * User sets: -D NTRIP_HOST=\"caster.com\" -D NTRIP_MOUNTPOINT=\"MOUNT\"
     * These are checked ONLY if local secrets were not found/valid. */
#if defined(NTRIP_HOST) && defined(NTRIP_MOUNTPOINT)
    if (NTRIP_HOST[0] != '\0' && NTRIP_MOUNTPOINT[0] != '\0') {
        strncpy(out->host, NTRIP_HOST, NTRIP_MAX_HOST_LEN - 1);
        out->host[NTRIP_MAX_HOST_LEN - 1] = '\0';

        #ifdef NTRIP_PORT
        out->port = (uint16_t)NTRIP_PORT;
        #else
        out->port = 2101;
        #endif

        strncpy(out->mountpoint, NTRIP_MOUNTPOINT, NTRIP_MAX_MOUNTPOINT_LEN - 1);
        out->mountpoint[NTRIP_MAX_MOUNTPOINT_LEN - 1] = '\0';

        #ifdef NTRIP_USER
        strncpy(out->username, NTRIP_USER, NTRIP_MAX_CRED_LEN - 1);
        out->username[NTRIP_MAX_CRED_LEN - 1] = '\0';
        #endif

        #ifdef NTRIP_PASSWORD
        strncpy(out->password, NTRIP_PASSWORD, NTRIP_MAX_CRED_LEN - 1);
        out->password[NTRIP_MAX_CRED_LEN - 1] = '\0';
        #endif

        return NTRIP_CFG_SRC_COMPILE_TIME;
    }
#endif

    /* No valid config found — NTRIP stays disabled */
    return NTRIP_CFG_SRC_MISSING;
}

/**
 * Check if a config is ready (host && mountpoint non-empty).
 * Re-implements the ntrip_client_configure() validity check
 * so callers can check before calling configure().
 */
static inline bool ntrip_config_is_ready(const ntrip_client_config_t* cfg)
{
    if (cfg == NULL) return false;
    return (cfg->host[0] != '\0' && cfg->mountpoint[0] != '\0');
}

#ifdef __cplusplus
}
#endif

#endif /* NAV_NTRIP_CONFIG_H */
