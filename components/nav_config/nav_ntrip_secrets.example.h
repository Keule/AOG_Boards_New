/* ========================================================================
 * nav_ntrip_secrets.example.h — NTRIP Configuration Template
 *
 * NAV-ETH-BRINGUP-001-R2 WP-F/J
 *
 * This is the TEMPLATE file. Copy it to nav_ntrip_secrets.local.h and
 * fill in your actual NTRIP credentials:
 *
 *   cp nav_ntrip_secrets.example.h nav_ntrip_secrets.local.h
 *
 * The .local.h file is gitignored and MUST NOT be committed.
 * This template file is committed and contains ONLY placeholders.
 *
 * Which fields must be set for NTRIP to start:
 *   NTRIP_HOST       — Caster hostname or IP (REQUIRED)
 *   NTRIP_PORT       — Caster port (default: 2101)
 *   NTRIP_MOUNTPOINT — Mountpoint path (REQUIRED)
 *   NTRIP_USER       — Username (optional, depends on caster)
 *   NTRIP_PASSWORD   — Password (NEVER commit this)
 *   NTRIP_GGA_INTERVAL_MS — GGA sentence send interval (default: 1000)
 * ======================================================================== */

#ifndef NAV_NTRIP_SECRETS_LOCAL_H
#define NAV_NTRIP_SECRETS_LOCAL_H

/* ---- NTRIP Caster Connection ---- */
#define NTRIP_HOST            ""
#define NTRIP_PORT            2101
#define NTRIP_MOUNTPOINT      ""
#define NTRIP_USER            ""
#define NTRIP_PASSWORD        ""

/* ---- GGA Send Interval (ms) ---- */
#define NTRIP_GGA_INTERVAL_MS  1000

#endif /* NAV_NTRIP_SECRETS_LOCAL_H */
