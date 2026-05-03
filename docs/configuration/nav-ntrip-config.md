# NTRIP Configuration (NAV-ETH-BRINGUP-001-R2 WP-F/J)

## Overview

NTRIP (Networked Transport of RTCM via Internet Protocol) provides RTCM
correction data for RTK GNSS positioning. The firmware reads NTRIP connection
parameters from a **local, non-versioned secrets file**.

## Configuration File Location

| File | Versioned | Purpose |
|------|-----------|---------|
| `components/nav_config/nav_ntrip_secrets.example.h` | **Yes** | Template with placeholders |
| `components/nav_config/nav_ntrip_secrets.local.h` | **No** (.gitignore) | Real credentials |

## Setup

1. Copy the template:
   ```
   cp components/nav_config/nav_ntrip_secrets.example.h \
      components/nav_config/nav_ntrip_secrets.local.h
   ```

2. Edit `nav_ntrip_secrets.local.h` and fill in your credentials:
   ```c
   #define NTRIP_HOST            "rtk.example.com"
   #define NTRIP_PORT            2101
   #define NTRIP_MOUNTPOINT      "/MOUNTPOINT"
   #define NTRIP_USER            "myuser"
   #define NTRIP_PASSWORD        "mypassword"
   #define NTRIP_GGA_INTERVAL_MS  1000
   ```

3. Rebuild: `pio run -e nav_esp32_t_eth_lite`

## Required Fields

| Field | Required | Default | Description |
|-------|----------|---------|-------------|
| `NTRIP_HOST` | **Yes** | `""` | Caster hostname or IP address |
| `NTRIP_PORT` | No | `2101` | Caster port |
| `NTRIP_MOUNTPOINT` | **Yes** | `""` | Mountpoint path (e.g., `/MY_MOUNT`) |
| `NTRIP_USER` | No | `""` | Username (if caster requires auth) |
| `NTRIP_PASSWORD` | No | `""` | Password |
| `NTRIP_GGA_INTERVAL_MS` | No | `1000` | GGA send interval in ms |

Both `NTRIP_HOST` and `NTRIP_MOUNTPOINT` must be non-empty for NTRIP to start.

## How It Works

At build time, CMake checks if `nav_ntrip_secrets.local.h` exists:
- **File found** → defines `NTRIP_SECRETS_AVAILABLE=1`, firmware loads real config
- **File not found** → NTRIP stays disabled with empty defaults

## Log Output

### Without local config (disabled):
```
NTRIP: no local secrets file — disabled (see nav_ntrip_secrets.example.h)
NTRIP: config_ready=0 state=disabled reason=empty_config
NTRIP_CFG: host=empty port=0 mount=empty user=empty password=empty
```

### With local config (ready):
```
NTRIP: config loaded from local secrets (host=rtk.example.com, mount=/MY_MOUNT)
NTRIP: config_ready=1 state=ready
NTRIP_CFG: host=set port=2101 mount=set user=set password=set
```

## Security Rules

- **NEVER** commit `nav_ntrip_secrets.local.h` to git
- **NEVER** log passwords — the firmware shows only `password=<set>` or `password=<empty>`
- **NEVER** include secrets in ZIP artifacts or chat logs
- The `.gitignore` entries protect against accidental commits:
  ```
  *secrets.local*
  *ntrip*.local*
  components/nav_config/nav_ntrip_secrets.local.h
  ```
