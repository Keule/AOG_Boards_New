#include <stddef.h>
#include <string.h>
#include "unity.h"
#include "gnss_cache.h"


static gnss_cache_t cache;

static void make_snapshot(gnss_snapshot_t* snap,
                          uint64_t timestamp_ms,
                          double latitude,
                          double longitude,
                          uint8_t satellites)
{
    gnss_snapshot_init(snap);
    snap->timestamp_ms = timestamp_ms;
    snap->latitude = latitude;
    snap->longitude = longitude;
    snap->altitude = 12.5;
    snap->speed_ms = 1.5;
    snap->course_deg = 45.0;
    snap->satellites = satellites;
    snap->position_valid = true;
    snap->motion_valid = true;
    snap->accuracy_valid = true;
    snap->valid = true;
    snap->fresh = true;
    snap->fix_quality = GNSS_FIX_RTK_FIXED;
    snap->rtk_status = GNSS_RTK_FIXED;
    snap->status_reason = GNSS_REASON_NONE;
}

void setUp(void)
{
    memset(&cache, 0xAA, sizeof(cache));
    gnss_cache_init(&cache);
}

void tearDown(void)
{
}

void test_init_sets_indices_zero(void)
{
    TEST_ASSERT_EQUAL(0, cache.write_idx);
    TEST_ASSERT_EQUAL(0, cache.read_idx);
}

void test_init_clears_buffers(void)
{
    const gnss_snapshot_t* snap = gnss_cache_read(&cache);
    TEST_ASSERT_NOT_NULL(snap);
    TEST_ASSERT_FALSE(snap->valid);
    TEST_ASSERT_EQUAL(0, snap->timestamp_ms);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, snap->latitude);
}

void test_read_before_publish_returns_initial_buffer(void)
{
    const gnss_snapshot_t* snap = gnss_cache_read(&cache);
    TEST_ASSERT_NOT_NULL(snap);
    TEST_ASSERT_FALSE(snap->position_valid);
    TEST_ASSERT_FALSE(snap->motion_valid);
    TEST_ASSERT_EQUAL(GNSS_FIX_NONE, snap->fix_quality);
}

void test_publish_updates_read_buffer(void)
{
    gnss_snapshot_t snap;
    make_snapshot(&snap, 100U, 48.1173, 11.5167, 8U);

    gnss_cache_publish(&cache, &snap);

    const gnss_snapshot_t* read = gnss_cache_read(&cache);
    TEST_ASSERT_NOT_NULL(read);
    TEST_ASSERT_EQUAL(100U, read->timestamp_ms);
    TEST_ASSERT_TRUE(read->valid);
    TEST_ASSERT_EQUAL_DOUBLE(48.1173, read->latitude);
    TEST_ASSERT_EQUAL(8, read->satellites);
}

void test_publish_swaps_indices(void)
{
    gnss_snapshot_t snap;
    make_snapshot(&snap, 200U, 1.0, 2.0, 4U);

    gnss_cache_publish(&cache, &snap);

    TEST_ASSERT_EQUAL(1, cache.write_idx);
    TEST_ASSERT_EQUAL(0, cache.read_idx);
}

void test_double_publish_returns_latest_snapshot(void)
{
    gnss_snapshot_t first;
    gnss_snapshot_t second;
    make_snapshot(&first, 111U, 10.0, 20.0, 3U);
    make_snapshot(&second, 222U, 30.0, 40.0, 6U);

    gnss_cache_publish(&cache, &first);
    gnss_cache_publish(&cache, &second);

    const gnss_snapshot_t* read = gnss_cache_read(&cache);
    TEST_ASSERT_EQUAL(222U, read->timestamp_ms);
    TEST_ASSERT_EQUAL_DOUBLE(30.0, read->latitude);
    TEST_ASSERT_EQUAL_DOUBLE(40.0, read->longitude);
    TEST_ASSERT_EQUAL(6, read->satellites);
}

void test_read_unchanged_after_publish(void)
{
    gnss_snapshot_t first;
    gnss_snapshot_t second;
    make_snapshot(&first, 300U, 50.0, 60.0, 5U);
    make_snapshot(&second, 400U, 70.0, 80.0, 9U);

    gnss_cache_publish(&cache, &first);
    const gnss_snapshot_t* old_read = gnss_cache_read(&cache);
    TEST_ASSERT_EQUAL(300U, old_read->timestamp_ms);

    gnss_cache_publish(&cache, &second);

    TEST_ASSERT_EQUAL(300U, old_read->timestamp_ms);
    TEST_ASSERT_EQUAL_DOUBLE(50.0, old_read->latitude);
    TEST_ASSERT_EQUAL(400U, gnss_cache_read(&cache)->timestamp_ms);
}

void test_boundary_values(void)
{
    gnss_snapshot_t snap;
    make_snapshot(&snap, UINT64_MAX, -90.0, 180.0, 255U);
    snap.altitude = -1234.5;
    snap.speed_ms = 0.0;

    gnss_cache_publish(&cache, &snap);

    const gnss_snapshot_t* read = gnss_cache_read(&cache);
    TEST_ASSERT_EQUAL(UINT64_MAX, read->timestamp_ms);
    TEST_ASSERT_EQUAL_DOUBLE(-90.0, read->latitude);
    TEST_ASSERT_EQUAL_DOUBLE(180.0, read->longitude);
    TEST_ASSERT_EQUAL_DOUBLE(-1234.5, read->altitude);
    TEST_ASSERT_EQUAL(255, read->satellites);
}

void test_null_safety(void)
{
    gnss_snapshot_t snap;
    make_snapshot(&snap, 500U, 1.0, 1.0, 1U);

    gnss_cache_init(NULL);
    gnss_cache_publish(NULL, &snap);
    gnss_cache_publish(&cache, NULL);
    TEST_ASSERT_NULL(gnss_cache_read(NULL));
}

void test_concurrent_simulation(void)
{
    gnss_snapshot_t first;
    gnss_snapshot_t second;
    make_snapshot(&first, 600U, 11.0, 22.0, 7U);
    make_snapshot(&second, 700U, 33.0, 44.0, 8U);

    gnss_cache_publish(&cache, &first);
    const gnss_snapshot_t* read_before = gnss_cache_read(&cache);
    TEST_ASSERT_EQUAL(600U, read_before->timestamp_ms);

    gnss_cache_publish(&cache, &second);
    const gnss_snapshot_t* read_after = gnss_cache_read(&cache);

    TEST_ASSERT_EQUAL(600U, read_before->timestamp_ms);
    TEST_ASSERT_EQUAL(700U, read_after->timestamp_ms);
    TEST_ASSERT_TRUE(read_before != read_after);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_sets_indices_zero);
    RUN_TEST(test_init_clears_buffers);
    RUN_TEST(test_read_before_publish_returns_initial_buffer);
    RUN_TEST(test_publish_updates_read_buffer);
    RUN_TEST(test_publish_swaps_indices);
    RUN_TEST(test_double_publish_returns_latest_snapshot);
    RUN_TEST(test_read_unchanged_after_publish);
    RUN_TEST(test_boundary_values);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_concurrent_simulation);
    return UNITY_END();
}
