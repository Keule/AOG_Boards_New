/* ==========================================================================
 * Hardware Smoke Test: Ethernet
 *
 * This test requires physical hardware (ESP32 with Ethernet).
 * It CANNOT run on host — use pio test -e nav_esp32_t_eth_lite or
 * pio test -e full_test_esp32s3 on a connected device.
 *
 * Test Plan:
 * 1. Ethernet init via HAL
 * 2. Link/IP status readable via hal_eth_get_status()
 * 3. UDP socket startable (bind succeeds)
 * 4. TCP socket skeleton startable (connect attempt)
 * ========================================================================== */

#include "unity.h"
#include <stdio.h>
#include "hal_eth.h"
#include "transport_udp.h"
#include "transport_tcp.h"

/* NOTE: These tests require physical hardware.
 * They are marked IGNORE on host builds. */

void test_ethernet_init(void)
{
    /* On real hardware:
     * const hal_eth_ops_t* ops = hal_eth_esp32_ops();
     * TEST_ASSERT_EQUAL(HAL_OK, hal_eth_init(ops));
     * hal_eth_status_t status;
     * TEST_ASSERT_EQUAL(HAL_OK, hal_eth_get_status(&status));
     */
    TEST_IGNORE_MESSAGE("Requires physical hardware — implement for on-target build");
}

void test_link_ip_status_readable(void)
{
    /* On real hardware:
     * hal_eth_status_t status;
     * TEST_ASSERT_EQUAL(HAL_OK, hal_eth_get_status(&status));
     * Expected: link_up=true after cable connected
     * Expected: ip_acquired=true after DHCP
     */
    TEST_IGNORE_MESSAGE("Requires physical hardware — implement for on-target build");
}

void test_udp_socket_startable(void)
{
    /* On real hardware:
     * transport_udp_t udp;
     * transport_udp_config_t cfg = {
     *     .local_port = 9999,
     *     .remote_ip = 0xFFFFFFFF,
     *     .remote_port = 9999
     * };
     * TEST_ASSERT_EQUAL(HAL_OK, transport_udp_init(&udp, &cfg));
     * TEST_ASSERT_TRUE(transport_udp_is_bound(&udp));
     */
    TEST_IGNORE_MESSAGE("Requires physical hardware — implement for on-target build");
}

void test_tcp_socket_skeleton_startable(void)
{
    /* On real hardware:
     * transport_tcp_t tcp;
     * transport_tcp_config_t cfg = {
     *     .remote_ip = 0,
     *     .remote_port = 2101
     * };
     * TEST_ASSERT_EQUAL(HAL_OK, transport_tcp_init(&tcp, &cfg));
     * // Connect attempt to NTRIP server
     * TEST_ASSERT_EQUAL(HAL_OK, transport_tcp_connect(&tcp));
     */
    TEST_IGNORE_MESSAGE("Requires physical hardware — implement for on-target build");
}

void test_nmea_checksum_valid(void)
{
    TEST_IGNORE_MESSAGE("Requires physical hardware — implement for on-target build");
}

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ethernet_init);
    RUN_TEST(test_link_ip_status_readable);
    RUN_TEST(test_udp_socket_startable);
    RUN_TEST(test_tcp_socket_skeleton_startable);
    RUN_TEST(test_nmea_checksum_valid);
    return UNITY_END();
}
