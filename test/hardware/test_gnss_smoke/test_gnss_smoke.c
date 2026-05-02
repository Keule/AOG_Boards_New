/* ========================================================================
 * test_gnss_smoke — Hardware Smoke Test for GNSS (NAV-GNSS-VALID-001 Nacharbeit)
 *
 * Tests Variant A validity model on target.
 * All NMEA sentences use correct checksums.
 * ======================================================================== */

#include "unity.h"
#include "gnss_um980.h"
#include "gnss_snapshot.h"
#include "byte_ring_buffer.h"
#include <string.h>
#include <math.h>

static gnss_um980_t primary;
static gnss_um980_t secondary;
static uint8_t rx_storage[512];
static byte_ring_buffer_t rx_buffer;

void setUp(void)
{
    memset(&primary, 0, sizeof(primary));
    memset(&secondary, 0, sizeof(secondary));
    memset(rx_storage, 0, sizeof(rx_storage));
    byte_ring_buffer_init(&rx_buffer, rx_storage, sizeof(rx_storage));

    gnss_um980_init(&primary, 0, "hw_primary");
    gnss_um980_init(&secondary, 1, "hw_secondary");
}

void tearDown(void) {}

void test_hw_gga_rmc_valid_snapshot(void)
{
    gnss_um980_set_rx_source(&primary, &rx_buffer);

    const char* gga = "$GNGGA,092751.000,5321.6802,N,01339.4463,E,1,09,1.2,34.5,M,0.0,M,,*46\r\n";
    const char* rmc = "$GNRMC,092751.000,A,5321.6802,N,01339.4463,E,5.4,45.0,250125,,,A*4D\r\n";
    byte_ring_buffer_write(&rx_buffer, (const uint8_t*)gga, strlen(gga));
    byte_ring_buffer_write(&rx_buffer, (const uint8_t*)rmc, strlen(rmc));

    gnss_um980_service_step((runtime_component_t*)&primary, 92751000);

    const gnss_snapshot_t* snap = gnss_um980_get_snapshot(&primary);
    TEST_ASSERT_NOT_NULL(snap);
    TEST_ASSERT_TRUE(snap->position_valid);
    TEST_ASSERT_TRUE(snap->motion_valid);
    TEST_ASSERT_TRUE(snap->valid);
    TEST_ASSERT_TRUE(snap->fresh);
    TEST_ASSERT_EQUAL(GNSS_REASON_NONE, snap->status_reason);
    TEST_ASSERT_EQUAL(GNSS_FIX_SINGLE, snap->fix_quality);
    TEST_ASSERT_EQUAL(9, snap->satellites);
}

void test_hw_checksum_rejection(void)
{
    gnss_um980_set_rx_source(&primary, &rx_buffer);

    const char* bad = "$GNGGA,123519,4807.038,N,01131.000,E,1,04,1.0,50.0,M,0.0,M,,*FF\r\n";
    byte_ring_buffer_write(&rx_buffer, (const uint8_t*)bad, strlen(bad));

    gnss_um980_service_step((runtime_component_t*)&primary, 1000000);

    TEST_ASSERT_EQUAL(1, primary.checksum_errors);
    TEST_ASSERT_FALSE(primary.gga_valid);
    TEST_ASSERT_FALSE(primary.snapshot.position_valid);
}

void test_hw_rmc_void_motion_invalid(void)
{
    gnss_um980_set_rx_source(&primary, &rx_buffer);

    const char* gga = "$GNGGA,092751.000,5321.6802,N,01339.4463,E,1,09,1.2,34.5,M,0.0,M,,*46\r\n";
    const char* rmc = "$GNRMC,092751.000,V,,,,,,250125,,N*5A\r\n";
    byte_ring_buffer_write(&rx_buffer, (const uint8_t*)gga, strlen(gga));
    byte_ring_buffer_write(&rx_buffer, (const uint8_t*)rmc, strlen(rmc));

    gnss_um980_service_step((runtime_component_t*)&primary, 92751000);

    const gnss_snapshot_t* snap = gnss_um980_get_snapshot(&primary);
    TEST_ASSERT_TRUE(snap->position_valid);
    TEST_ASSERT_FALSE(snap->motion_valid);
    TEST_ASSERT_FALSE(snap->valid);
    TEST_ASSERT_EQUAL(GNSS_REASON_RMC_VOID, snap->status_reason);
}

void test_hw_dual_isolation(void)
{
    uint8_t rx2_storage[512];
    byte_ring_buffer_t rx2_buffer;
    byte_ring_buffer_init(&rx2_buffer, rx2_storage, sizeof(rx2_storage));

    gnss_um980_set_rx_source(&primary, &rx_buffer);
    gnss_um980_set_rx_source(&secondary, &rx2_buffer);

    const char* gga1 = "$GNGGA,100000,4807.038,N,01131.000,E,4,12,1.0,100.0,M,0.0,M,,*69\r\n";
    const char* rmc1 = "$GNRMC,100000,A,4807.038,N,01131.000,E,22.4,84.4,230394,003.1,W,A*15\r\n";
    const char* gga2 = "$GNGGA,200000,3412.500,S,11815.000,W,1,06,2.0,50.0,M,0.0,M,,*5D\r\n";
    const char* rmc2 = "$GNRMC,200000,V,,,,,,010125,,N*48\r\n";

    byte_ring_buffer_write(&rx_buffer, (const uint8_t*)gga1, strlen(gga1));
    byte_ring_buffer_write(&rx_buffer, (const uint8_t*)rmc1, strlen(rmc1));
    byte_ring_buffer_write(&rx2_buffer, (const uint8_t*)gga2, strlen(gga2));
    byte_ring_buffer_write(&rx2_buffer, (const uint8_t*)rmc2, strlen(rmc2));

    gnss_um980_service_step((runtime_component_t*)&primary, 100000000);
    gnss_um980_service_step((runtime_component_t*)&secondary, 200000000);

    TEST_ASSERT_TRUE(primary.snapshot.valid);
    TEST_ASSERT_TRUE(primary.snapshot.fresh);
    TEST_ASSERT_FALSE(secondary.snapshot.valid);  /* RMC void */
    TEST_ASSERT_EQUAL(GNSS_REASON_RMC_VOID, secondary.snapshot.status_reason);
}

void test_hw_correction_age(void)
{
    gnss_um980_set_rx_source(&primary, &rx_buffer);

    /* GGA with age_diff=46.9 (field present) */
    const char* gga = "$GNGGA,092751.000,5321.6802,N,01339.4463,E,1,09,1.2,34.5,M,,M,46.9,,*51\r\n";
    byte_ring_buffer_write(&rx_buffer, (const uint8_t*)gga, strlen(gga));

    gnss_um980_service_step((runtime_component_t*)&primary, 92751000);

    TEST_ASSERT_TRUE(primary.gga.age_diff_valid);
    TEST_ASSERT_TRUE(primary.snapshot.correction_age_valid);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hw_gga_rmc_valid_snapshot);
    RUN_TEST(test_hw_checksum_rejection);
    RUN_TEST(test_hw_rmc_void_motion_invalid);
    RUN_TEST(test_hw_dual_isolation);
    RUN_TEST(test_hw_correction_age);
    return UNITY_END();
}
