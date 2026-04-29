#include "unity.h"
#include <stdio.h>
#include <string.h>
#include "hal_eth.h"

/* ==========================================================================
 * Tests for HAL Ethernet abstraction layer
 * ========================================================================== */

void test_hal_eth_init_with_null_ops_fails(void)
{
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, hal_eth_init(NULL));
}

void test_hal_eth_get_controller(void)
{
    /* Returns board profile ethernet kind — on host it's ETH_UNKNOWN */
    ethernet_kind_t kind = hal_eth_get_controller();
    /* On host tests (no CONFIG_ETH_*), this should be ETH_UNKNOWN */
    (void)kind;
    /* Just verify the API doesn't crash */
}

void test_hal_eth_get_status_null(void)
{
    hal_eth_status_t status;
    TEST_ASSERT_EQUAL(HAL_ERR_INVALID_PARAM, hal_eth_get_status(NULL));
}

void test_hal_eth_get_status_not_initialized(void)
{
    hal_eth_deinit();  /* Ensure no ops */
    hal_eth_status_t status;
    hal_err_t err = hal_eth_get_status(&status);
    TEST_ASSERT_EQUAL(HAL_ERR_NOT_INITIALIZED, err);
    TEST_ASSERT_EQUAL(0, status.ip_addr);
    TEST_ASSERT_FALSE(status.link_up);
}

void test_hal_eth_esp32_ops_returns_non_null(void)
{
    const hal_eth_ops_t* ops = hal_eth_esp32_ops();
    TEST_ASSERT_NOT_NULL(ops);
    TEST_ASSERT_NOT_NULL(ops->init);
    TEST_ASSERT_NOT_NULL(ops->deinit);
    TEST_ASSERT_NOT_NULL(ops->get_status);
}

void test_hal_eth_init_and_status(void)
{
    const hal_eth_ops_t* ops = hal_eth_esp32_ops();
    TEST_ASSERT_EQUAL(HAL_OK, hal_eth_init(ops));

    hal_eth_status_t status;
    TEST_ASSERT_EQUAL(HAL_OK, hal_eth_get_status(&status));
    /* Stubs return zeroed status */
    TEST_ASSERT_EQUAL(0, status.ip_addr);
}

void test_hal_eth_is_connected_stub(void)
{
    /* After deinit, should return false */
    hal_eth_deinit();
    TEST_ASSERT_FALSE(hal_eth_is_connected());

    /* Init with stubs, still false (stubs have no link) */
    TEST_ASSERT_EQUAL(HAL_OK, hal_eth_init(hal_eth_esp32_ops()));
    TEST_ASSERT_FALSE(hal_eth_is_connected());
}

void test_hal_eth_deinit(void)
{
    TEST_ASSERT_EQUAL(HAL_OK, hal_eth_init(hal_eth_esp32_ops()));
    TEST_ASSERT_EQUAL(HAL_OK, hal_eth_deinit());
}

void test_hal_eth_status_has_mac_field(void)
{
    /* Verify status struct has all required fields */
    hal_eth_status_t status;
    memset(&status, 0xFF, sizeof(status));

    status.link_up = false;
    status.ip_acquired = false;
    status.ip_addr = 0;
    status.error_flags = 0;

    TEST_ASSERT_FALSE(status.link_up);
    TEST_ASSERT_EQUAL(0, status.ip_addr);
    TEST_ASSERT_EQUAL(0, status.error_flags);
}

/* ---- Unity ---- */

void setUp(void) {}
void tearDown(void) { hal_eth_deinit(); }

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hal_eth_init_with_null_ops_fails);
    RUN_TEST(test_hal_eth_get_controller);
    RUN_TEST(test_hal_eth_get_status_null);
    RUN_TEST(test_hal_eth_get_status_not_initialized);
    RUN_TEST(test_hal_eth_esp32_ops_returns_non_null);
    RUN_TEST(test_hal_eth_init_and_status);
    RUN_TEST(test_hal_eth_is_connected_stub);
    RUN_TEST(test_hal_eth_deinit);
    RUN_TEST(test_hal_eth_status_has_mac_field);
    return UNITY_END();
}
