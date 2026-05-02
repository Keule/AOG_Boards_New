# ========================================================================
# extra_scripts/nav_gate.py
#
# PlatformIO pre-build hook for ESP-IDF builds.
# Creates a marker file that CMakeLists.txt reads to decide which
# components to include.
#
# For NAV builds: excludes steering components from compilation.
# For STEER/FULL builds: builds all components (default behavior).
#
# NAV-TEST-SPLIT-001 WP-D: NAV-Build von Steering entkoppeln
# ========================================================================

import os
from pathlib import Path

try:
    from SCons.Script import Import
    Import("env")

    proj_dir = env.subst("$PROJECT_DIR")
    pio_env = env.subst("$PIOENV")

    # Determine build role from PlatformIO environment name
    if pio_env.startswith("nav_"):
        build_role = "NAVIGATION"
    elif pio_env.startswith("steer_"):
        build_role = "STEERING"
    elif pio_env.startswith("full_"):
        build_role = "FULL_TEST"
    else:
        build_role = "FULL_TEST"

    # Write marker file for CMakeLists.txt to read
    marker_file = Path(proj_dir) / ".aog_build_role"
    marker_file.write_text(build_role)

except Exception:
    pass
