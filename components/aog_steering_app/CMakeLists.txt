idf_component_register(
    SRCS "aog_steering_app.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components transport_udp protocol_aog steering_control safety_failsafe
)
