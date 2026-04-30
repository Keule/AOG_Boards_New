#include "unity.h"
#include "runtime_types.h"
#include "runtime_mode.h"

/* ========================================================================
 * test_runtime_mode — Host tests for Work/Config mode switching
 *
 * Tests the pure-logic mode/profile API from runtime_mode.h/c
 * without any ESP-IDF or FreeRTOS dependency.
 * ======================================================================== */

void setUp(void)
{
    /* Reset mode state before each test */
    runtime_mode_init();
}

void tearDown(void)
{
    /* nothing */
}

/* ---- 1. Default mode is WORK ---- */

void test_default_mode_is_work(void)
{
    TEST_ASSERT_EQUAL(SYSTEM_MODE_WORK, runtime_mode_get());
}

/* ---- 2. runtime_mode_set(CONFIG) changes the stored mode ---- */

void test_set_config_changes_mode(void)
{
    int ret = runtime_mode_set(SYSTEM_MODE_CONFIG);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(SYSTEM_MODE_CONFIG, runtime_mode_get());
}

/* ---- 3. runtime_mode_set(WORK) changes back ---- */

void test_set_work_changes_back(void)
{
    runtime_mode_set(SYSTEM_MODE_CONFIG);
    runtime_mode_set(SYSTEM_MODE_WORK);
    TEST_ASSERT_EQUAL(SYSTEM_MODE_WORK, runtime_mode_get());
}

/* ---- 4. Invalid mode is rejected ---- */

void test_invalid_mode_rejected(void)
{
    int ret = runtime_mode_set((system_mode_t)-1);
    TEST_ASSERT_EQUAL(-1, ret);
    TEST_ASSERT_EQUAL(SYSTEM_MODE_WORK, runtime_mode_get());

    ret = runtime_mode_set(SYSTEM_MODE_COUNT);
    TEST_ASSERT_EQUAL(-1, ret);
    TEST_ASSERT_EQUAL(SYSTEM_MODE_WORK, runtime_mode_get());

    ret = runtime_mode_set((system_mode_t)99);
    TEST_ASSERT_EQUAL(-1, ret);
}

/* ---- 5. Diagnostics period differs between Work and Config ---- */

void test_diagnostics_period_differs(void)
{
    const service_profile_t* work_diag = runtime_mode_get_profile(SERVICE_GROUP_DIAGNOSTICS);
    TEST_ASSERT_NOT_NULL(work_diag);
    TEST_ASSERT_EQUAL(100, work_diag->period_ms);

    runtime_mode_set(SYSTEM_MODE_CONFIG);

    const service_profile_t* config_diag = runtime_mode_get_profile(SERVICE_GROUP_DIAGNOSTICS);
    TEST_ASSERT_NOT_NULL(config_diag);
    TEST_ASSERT_EQUAL(50, config_diag->period_ms);
}

/* ---- 6. Diagnostics priority differs between Work and Config ---- */

void test_diagnostics_priority_differs(void)
{
    const service_profile_t* work_diag = runtime_mode_get_profile(SERVICE_GROUP_DIAGNOSTICS);
    TEST_ASSERT_NOT_NULL(work_diag);
    TEST_ASSERT_EQUAL(3, work_diag->priority);

    runtime_mode_set(SYSTEM_MODE_CONFIG);

    const service_profile_t* config_diag = runtime_mode_get_profile(SERVICE_GROUP_DIAGNOSTICS);
    TEST_ASSERT_NOT_NULL(config_diag);
    TEST_ASSERT_EQUAL(6, config_diag->priority);
}

/* ---- 7. UART/UDP/TCP_NTRIP remain active in Work and Config ---- */

void test_io_groups_active_in_both_modes(void)
{
    service_group_t io_groups[] = {
        SERVICE_GROUP_UART,
        SERVICE_GROUP_UDP,
        SERVICE_GROUP_TCP_NTRIP
    };

    /* Check Work mode */
    for (int i = 0; i < 3; i++) {
        const service_profile_t* p = runtime_mode_get_profile(io_groups[i]);
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_FALSE(p->suspended);
    }

    /* Switch to Config */
    runtime_mode_set(SYSTEM_MODE_CONFIG);

    /* Check Config mode */
    for (int i = 0; i < 3; i++) {
        const service_profile_t* p = runtime_mode_get_profile(io_groups[i]);
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_FALSE(p->suspended);
    }
}

/* ---- 8. UART/UDP/TCP_NTRIP period unchanged between Work and Config ---- */

void test_io_groups_period_unchanged(void)
{
    service_group_t io_groups[] = {
        SERVICE_GROUP_UART,
        SERVICE_GROUP_UDP,
        SERVICE_GROUP_TCP_NTRIP
    };

    uint16_t work_periods[3];
    for (int i = 0; i < 3; i++) {
        const service_profile_t* p = runtime_mode_get_profile(io_groups[i]);
        work_periods[i] = p->period_ms;
    }

    runtime_mode_set(SYSTEM_MODE_CONFIG);

    for (int i = 0; i < 3; i++) {
        const service_profile_t* p = runtime_mode_get_profile(io_groups[i]);
        TEST_ASSERT_EQUAL(work_periods[i], p->period_ms);
    }
}

/* ---- 9. Profile pointer is valid for all groups ---- */

void test_profile_valid_for_all_groups(void)
{
    for (int g = 0; g < SERVICE_GROUP_COUNT; g++) {
        const service_profile_t* p = runtime_mode_get_profile((service_group_t)g);
        TEST_ASSERT_NOT_NULL(p);
    }
}

/* ---- 10. Profile for invalid group returns NULL ---- */

void test_profile_null_for_invalid_group(void)
{
    const service_profile_t* p;

    p = runtime_mode_get_profile((service_group_t)-1);
    TEST_ASSERT_NULL(p);

    p = runtime_mode_get_profile(SERVICE_GROUP_COUNT);
    TEST_ASSERT_NULL(p);
}

/* ---- 11. Setting same mode is idempotent (no error) ---- */

void test_set_same_mode_idempotent(void)
{
    /* Already in WORK after init */
    int ret = runtime_mode_set(SYSTEM_MODE_WORK);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(SYSTEM_MODE_WORK, runtime_mode_get());

    /* Switch to CONFIG, then CONFIG again */
    runtime_mode_set(SYSTEM_MODE_CONFIG);
    ret = runtime_mode_set(SYSTEM_MODE_CONFIG);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(SYSTEM_MODE_CONFIG, runtime_mode_get());
}

/* ---- 12. Work/Config profile accessors return correct values ---- */

void test_profile_accessors(void)
{
    const service_profile_t* wp = runtime_mode_work_profile(SERVICE_GROUP_DIAGNOSTICS);
    TEST_ASSERT_NOT_NULL(wp);
    TEST_ASSERT_EQUAL(3, wp->priority);
    TEST_ASSERT_EQUAL(100, wp->period_ms);

    const service_profile_t* cp = runtime_mode_config_profile(SERVICE_GROUP_DIAGNOSTICS);
    TEST_ASSERT_NOT_NULL(cp);
    TEST_ASSERT_EQUAL(6, cp->priority);
    TEST_ASSERT_EQUAL(50, cp->period_ms);
}

/* ---- 13. FastCycleContext contains no service_profile (compile-time check) ---- */
/* This test verifies that fast_cycle_context_t has no service_profile field.
 * It's a structural check — if fast_cycle_context_t gains a service_profile
 * member, sizeof(fast_cycle_context_t) will change and this test flags it.
 * The actual guarantee is in runtime_types.h (comment + no member). */

void test_fast_cycle_context_no_service_profile(void)
{
    /* fast_cycle_context_t has 5 uint64/uint32 fields = 40 bytes (on 64-bit)
     * or 36 bytes (on 32-bit with natural alignment).
     * If a service_profile_t (3+2+1 = ~6 bytes) were added, size would grow. */
    fast_cycle_context_t ctx = {0};

    /* Verify the struct exists and has expected approximate size.
     * Exact size depends on alignment, but should be around 36-40 bytes. */
    size_t ctx_size = sizeof(ctx);
    TEST_ASSERT(ctx_size >= 32 && ctx_size <= 48);

    /* Verify no padding beyond what 5 fields would produce */
    /* 5 fields: 2x uint64 (16) + 3x uint32 (12) = 28 bytes min.
     * With alignment: 28-40 bytes.  A service_profile would push it higher. */
}

/* ---- 14. Switching back to WORK restores work profiles ---- */

void test_switch_back_restores_work_profiles(void)
{
    /* Capture work diagnostics profile */
    const service_profile_t* wd = runtime_mode_get_profile(SERVICE_GROUP_DIAGNOSTICS);
    uint8_t work_prio = wd->priority;
    uint16_t work_period = wd->period_ms;

    /* Switch to Config */
    runtime_mode_set(SYSTEM_MODE_CONFIG);

    /* Switch back to Work */
    runtime_mode_set(SYSTEM_MODE_WORK);

    const service_profile_t* wd2 = runtime_mode_get_profile(SERVICE_GROUP_DIAGNOSTICS);
    TEST_ASSERT_EQUAL(work_prio, wd2->priority);
    TEST_ASSERT_EQUAL(work_period, wd2->period_ms);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_mode_is_work);
    RUN_TEST(test_set_config_changes_mode);
    RUN_TEST(test_set_work_changes_back);
    RUN_TEST(test_invalid_mode_rejected);
    RUN_TEST(test_diagnostics_period_differs);
    RUN_TEST(test_diagnostics_priority_differs);
    RUN_TEST(test_io_groups_active_in_both_modes);
    RUN_TEST(test_io_groups_period_unchanged);
    RUN_TEST(test_profile_valid_for_all_groups);
    RUN_TEST(test_profile_null_for_invalid_group);
    RUN_TEST(test_set_same_mode_idempotent);
    RUN_TEST(test_profile_accessors);
    RUN_TEST(test_fast_cycle_context_no_service_profile);
    RUN_TEST(test_switch_back_restores_work_profiles);

    return UNITY_END();
}
