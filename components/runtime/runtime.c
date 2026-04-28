#include "runtime.h"
#include "task_fast.h"

#include "runtime_component.h"
#include "gnss_um980.h"
#include "gnss_dual_heading.h"
#include "rtcm_router.h"
#include "aog_navigation_app.h"

static int navigation_role_enabled(void)
{
#if defined(DEVICE_ROLE_NAVIGATION) || defined(DEVICE_ROLE_FULL_TEST)
    return 1;
#else
    return 0;
#endif
}

void runtime_init(void)
{
    if (navigation_role_enabled() == 0) {
        return;
    }

    gnss_um980_init();
    gnss_dual_heading_init();
    rtcm_router_init();
    aog_navigation_app_init();

    runtime_component_register(gnss_um980_component());
    runtime_component_register(gnss_dual_heading_component());
    runtime_component_register(rtcm_router_component());
    runtime_component_register(aog_navigation_app_component());
}

void runtime_start(void)
{
    task_fast_start();
}
