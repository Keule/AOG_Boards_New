idf_component_register(
    SRCS "nav_config.c"
    INCLUDE_DIRS "."
    REQUIRES ntrip_client
)

# Check if local secrets file exists
set(LOCAL_SECRETS "${CMAKE_CURRENT_SOURCE_DIR}/nav_ntrip_secrets.local.h")

if(EXISTS "${LOCAL_SECRETS}")
    message(STATUS "nav_config: Found local NTRIP secrets file — NTRIP config will be active")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE NTRIP_SECRETS_AVAILABLE=1)
else()
    message(STATUS "nav_config: No local NTRIP secrets file — NTRIP will stay disabled (copy .example.h to .local.h)")
endif()
