# ========================================================================
# hw_runtime_diag CMakeLists.txt
# NAV-HW-RUNTIME-DIAG-001: Periodic hardware runtime diagnostics
# ========================================================================

idf_component_register(
    SRCS "hw_runtime_diag.c"
    INCLUDE_DIRS "."
    REQUIRES log esp_timer runtime_components runtime_stats transport_uart
               gnss_um980 gnss_dual_heading aog_navigation_app
)
