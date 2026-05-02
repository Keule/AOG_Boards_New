# ========================================================================
# extra_scripts/native_test.py
#
# PlatformIO pre-build hook for native host tests.
# Adds component source files and include paths to the native build.
#
# NOTE: This script runs with 'pio run' but NOT with 'pio test'.
# For 'pio test', components are provided via lib_extra_dirs + library.json.
# This script is kept for reference and potential future 'pio run' usage.
# ========================================================================

import os
from pathlib import Path

def _add_unity_header(env, proj_dir):
    """Add local unity.h from test/host (MUST be first to override PIO's)."""
    test_host = Path(proj_dir) / "test" / "host"
    if test_host.is_dir():
        env.Append(CPPPATH=[str(test_host)])

def _scan_components(env, proj_dir):
    """Add component sources and include paths for native build."""
    comp_root = Path(proj_dir) / "components"
    needed = [
        "protocol_nmea", "runtime_buffers", "runtime_snapshot",
        "runtime_components", "runtime_types", "gnss_um980",
        "gnss_snapshot", "gnss_dual_heading", "board_profiles",
        "rtcm_router", "nav_rtcm_wiring", "protocol_aog",
        "aog_navigation_app", "transport_uart", "transport_tcp",
        "nav_diagnostics",
    ]
    for name in needed:
        comp_dir = comp_root / name
        if not comp_dir.is_dir():
            continue
        env.Append(CPPPATH=[str(comp_dir)])
        for src in comp_dir.glob("*.c"):
            env.Append(CSRC_FILES=[str(src)])

def _add_mocks(env, proj_dir):
    """Add mock implementations that override real components."""
    mocks_dir = Path(proj_dir) / "test" / "host" / "mocks"
    if not mocks_dir.is_dir():
        return
    env.Append(CPPPATH=[str(mocks_dir)])
    for src in mocks_dir.glob("*.c"):
        env.Append(CSRC_FILES=[str(src)])

# ---- Entry point ----
try:
    from SCons.Script import Import
    Import("env")
    proj_dir = env.subst("$PROJECT_DIR")
    _add_unity_header(env, proj_dir)
    _scan_components(env, proj_dir)
    _add_mocks(env, proj_dir)
except Exception:
    pass
