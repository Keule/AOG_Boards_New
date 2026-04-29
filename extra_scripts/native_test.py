"""PlatformIO extra_scripts for native test environment.

Compiles pure-C component sources into a static library and links
tests against it.  No ESP-IDF source files are included.
Mock implementations override ESP-dependent components.
"""

import os

Import("env")

proj_dir = env.subst("$PROJECT_DIR")
build_dir = os.path.join(proj_dir, ".pio", "build", "native")

# ---------------------------------------------------------------------------
# Component include directories (headers only, no ESP-IDF transitive deps)
# ---------------------------------------------------------------------------
component_includes = [
    "components/hal_backend",
    "components/hal_uart",
    "components/board_profiles",
    "components/runtime_types",
    "components/runtime_components",
    "components/runtime_buffers",
    "components/transport_uart",
    "components/ntrip_client",
    "components/rtcm_router",
    "components/protocol_rtcm",
    "components/gnss_um980",
    "components/protocol_nmea",
]

for d in component_includes:
    path = os.path.join(proj_dir, d)
    if os.path.isdir(path):
        env.Append(CPPPATH=[path])

# ---------------------------------------------------------------------------
# Mock / stub include directory
# ---------------------------------------------------------------------------
mock_dir = os.path.join(proj_dir, "test", "host", "mocks")
if os.path.isdir(mock_dir):
    env.Append(CPPPATH=[mock_dir])

# ---------------------------------------------------------------------------
# Pure-C component sources + mocks — compiled into a static library
# ---------------------------------------------------------------------------
lib_sources = [
    "components/hal_backend/hal_backend.c",
    "components/hal_uart/hal_uart.c",
    "components/hal_uart/hal_uart_stub.c",
    "components/runtime_components/runtime_component.c",
    "components/runtime_buffers/byte_ring_buffer.c",
    "components/transport_uart/transport_uart.c",
    "components/ntrip_client/ntrip_client.c",
    "components/rtcm_router/rtcm_router.c",
    "components/protocol_rtcm/rtcm_passthrough.c",
    "test/host/mocks/board_profile_mock.c",
]

abs_sources = []
for src in lib_sources:
    path = os.path.join(proj_dir, src)
    if os.path.isfile(path) and path.endswith(".c"):
        abs_sources.append(path)

# Build the static library using SCons
lib_env = env.Clone()

# Ensure the library output directory exists
os.makedirs(build_dir, exist_ok=True)

lib_env.Append(CPPPATH=[os.path.join(proj_dir, d) for d in component_includes])
if os.path.isdir(mock_dir):
    lib_env.Append(CPPPATH=[mock_dir])

lib_env.StaticLibrary(
    target=os.path.join(build_dir, "libtestcomponents"),
    source=abs_sources,
)

# Link all test executables against this library
env.Append(LIBS=["testcomponents"])
env.Append(LIBPATH=[build_dir])
