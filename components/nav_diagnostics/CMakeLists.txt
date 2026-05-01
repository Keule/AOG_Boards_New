idf_component_register(
    SRCS "nav_health.c"
         "nav_diag_log.c"
         "nav_recovery.c"
    INCLUDE_DIRS "."
    REQUIRES runtime_components
               transport_uart
               gnss_um980
               gnss_dual_heading
               ntrip_client
               rtcm_router
               aog_navigation_app
               transport_tcp
               runtime_buffers
)
