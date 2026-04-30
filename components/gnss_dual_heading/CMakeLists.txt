idf_component_register(
    SRCS "gnss_dual_heading.c"
    INCLUDE_DIRS "."
    REQUIRES protocol_nmea gnss_um980 runtime_components runtime_snapshot
)
