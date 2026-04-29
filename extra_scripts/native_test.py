"""
PlatformIO extra_script for the [env:native] test environment.

Injects component include paths and source files into the SCons build
so that pio test -e native can find headers and link against the
component implementations without depending on ESP-IDF.

This approach works uniformly on:
  - Windows (MSYS2 MinGW / Visual Studio)
  - Linux / macOS
  - GitHub Actions CI
"""

Import("env")

import os
import glob

proj = env.subst("$PROJECT_DIR")

# Components required by the host tests (dependency order)
COMPONENTS = [
    "runtime_types",       # header-only (runtime_types.h, fast_cycle_context_t)
    "runtime_components",  # runtime_component.c/h
    "runtime_buffers",     # byte_ring_buffer.c/h
    "runtime_snapshot",    # snapshot_buffer.c/h
    "protocol_nmea",       # nmea_parser.c/h
    "protocol_rtcm",       # rtcm_passthrough.c/h
    "ntrip_client",        # ntrip_client.c/h
    "rtcm_router",         # rtcm_router.c/h
    "gnss_um980",          # gnss_um980.c/h
]

for comp in COMPONENTS:
    comp_dir = os.path.join(proj, "components", comp)
    if not os.path.isdir(comp_dir):
        continue

    # Add include path so headers are found
    env.AppendUnique(CPPPATH=[comp_dir])

    # Add .c source files so they are compiled and linked
    for src_file in glob.glob(os.path.join(comp_dir, "*.c")):
        env.Append(CSRC=[os.path.abspath(src_file)])
