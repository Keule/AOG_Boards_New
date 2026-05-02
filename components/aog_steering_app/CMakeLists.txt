
# NAV-TEST-SPLIT-001 WP-D: Build gating
# For NAV builds, this component is excluded (STEER-MIG-001 scope).
set(_aog_role "FULL_TEST")
if(EXISTS "${CMAKE_SOURCE_DIR}/.aog_build_role")
    file(STRINGS "${CMAKE_SOURCE_DIR}/.aog_build_role" _aog_role)
endif()
if(NOT _aog_role STREQUAL "NAVIGATION")

idf_component_register(
    SRCS "aog_steering_app.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components runtime_buffers runtime_snapshot protocol_aog
)
endif()
