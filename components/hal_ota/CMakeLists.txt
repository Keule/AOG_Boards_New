idf_component_register(
    SRCS "hal_ota.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend app_update
)
