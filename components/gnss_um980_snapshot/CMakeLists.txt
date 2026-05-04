idf_component_register(
    SRCS "gnss_um980_snapshot.c"
    INCLUDE_DIRS "include"
    REQUIRES log freertos gnss_um980_control
)
