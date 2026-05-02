cmake_minimum_required(VERSION 3.16)

# ========================================================================
# NAV-TEST-SPLIT-001 WP-D: Build Gating
#
# Read build role from marker file created by extra_scripts/nav_gate.py.
# Steering components check this marker and skip registration for NAV builds.
# ========================================================================

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Publish build role so components can check it
set(AOG_BUILD_ROLE "FULL_TEST" CACHE STRING "Build role: NAVIGATION, STEERING, or FULL_TEST")
if(EXISTS "${CMAKE_SOURCE_DIR}/.aog_build_role")
    file(STRINGS "${CMAKE_SOURCE_DIR}/.aog_build_role" AOG_BUILD_ROLE)
    message(STATUS "AOG build role: ${AOG_BUILD_ROLE}")
endif()

if(AOG_BUILD_ROLE STREQUAL "NAVIGATION")
    message(STATUS "NAV build: steering components will self-gate (STEER-MIG-001 scope)")
endif()

project(AOG_ESP_Multiboard)
