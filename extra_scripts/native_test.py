# ========================================================================
# extra_scripts/native_test.py
#
# PlatformIO pre-build hook for native host tests.
# Adds component source files and include paths to the native build,
# since ESP-IDF's idf_component_register() is not available in native.
#
# Usage: Referenced via platformio.ini [env:native] extra_scripts = pre:...
# ========================================================================

import os
from pathlib import Path

def _scan_components(env, proj_dir):
    """Add component sources and include paths for native build."""
    comp_root = Path(proj_dir) / "components"

    # Components used by native tests (pure C, no ESP-IDF deps)
    needed = [
        "protocol_nmea",
        "runtime_buffers",
        "runtime_snapshot",
        "runtime_components",
        "runtime_types",
        "gnss_um980",
        "gnss_snapshot",
        "gnss_dual_heading",
        "board_profiles",
        "rtcm_router",
        "nav_rtcm_wiring",
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

def _add_unity_header(env, proj_dir):
    """Add local unity.h from test/host."""
    test_host = Path(proj_dir) / "test" / "host"
    if test_host.is_dir():
        env.Append(CPPPATH=[str(test_host)])

# ---- Entry point ----

proj_dir = env.subst("$PROJECT_DIR")

_scan_components(env, proj_dir)
_add_mocks(env, proj_dir)
_add_unity_header(env, proj_dir)
