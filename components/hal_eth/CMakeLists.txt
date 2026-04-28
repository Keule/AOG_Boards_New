idf_component_register(
    SRCS "hal_eth.c"
    INCLUDE_DIRS "."
    REQUIRES hal_backend board_profiles
)
