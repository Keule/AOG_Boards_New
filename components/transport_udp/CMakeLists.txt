idf_component_register(
    SRCS "transport_udp.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_queue aog_navigation_app
)
