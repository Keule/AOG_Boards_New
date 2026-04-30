#include "unity.h"
#include "rtcm_router.h"
#include "byte_ring_buffer.h"
#include <string.h>

static rtcm_router_t router;
static uint8_t src_storage[256];
static byte_ring_buffer_t source;
static uint8_t out1_storage[128];
static uint8_t out2_storage[128];
static byte_ring_buffer_t out1;
static byte_ring_buffer_t out2;

void setUp(void)
{
    memset(&router, 0, sizeof(router));
    rtcm_router_init(&router);

    memset(src_storage, 0, sizeof(src_storage));
    byte_ring_buffer_init(&source, src_storage, sizeof(src_storage));
    rtcm_router_set_source(&router, &source);

    memset(out1_storage, 0, sizeof(out1_storage));
    memset(out2_storage, 0, sizeof(out2_storage));
    byte_ring_buffer_init(&out1, out1_storage, sizeof(out1_storage));
    byte_ring_buffer_init(&out2, out2_storage, sizeof(out2_storage));
}

void tearDown(void) {}

/* ---- Init ---- */

void test_init_resets_state(void)
{
    /* Re-init to verify that init properly resets state
     * (setUp already called init + set_source) */
    rtcm_router_init(&router);
    TEST_ASSERT_NULL(router.rtcm_source);
    TEST_ASSERT_EQUAL(0, router.output_count);
    TEST_ASSERT_NOT_NULL(rtcm_router_get_stats(&router));
}

void test_init_null_does_not_crash(void)
{
    rtcm_router_init(NULL);
    TEST_PASS();
}

/* ---- Add output ---- */

void test_add_output_returns_index(void)
{
    int idx = rtcm_router_add_output(&router, &out1);
    TEST_ASSERT_EQUAL(0, idx);
    TEST_ASSERT_EQUAL(1, router.output_count);

    idx = rtcm_router_add_output(&router, &out2);
    TEST_ASSERT_EQUAL(1, idx);
    TEST_ASSERT_EQUAL(2, router.output_count);
}

void test_add_output_max_exceeded(void)
{
    rtcm_router_add_output(&router, &out1);
    rtcm_router_add_output(&router, &out2);
    int idx = rtcm_router_add_output(&router, &out1); /* 3rd output, max is 2 */
    TEST_ASSERT_EQUAL(-2, idx);
}

void test_add_output_null_returns_negative(void)
{
    TEST_ASSERT_EQUAL(-1, rtcm_router_add_output(NULL, &out1));
    TEST_ASSERT_EQUAL(-1, rtcm_router_add_output(&router, NULL));
}

/* ---- Set source ---- */

void test_set_source(void)
{
    rtcm_router_set_source(&router, &source);
    TEST_ASSERT_EQUAL_PTR(&source, router.rtcm_source);
}

void test_set_source_null_does_not_crash(void)
{
    rtcm_router_set_source(NULL, &source);
    rtcm_router_set_source(&router, NULL);
    TEST_PASS();
}

/* ---- Service step: forward data ---- */

void test_forward_to_single_output(void)
{
    rtcm_router_add_output(&router, &out1);

    uint8_t rtcm_data[] = {0xD3, 0x00, 0x13, 0x3E, 0xD7};
    byte_ring_buffer_write(&source, rtcm_data, sizeof(rtcm_data));

    rtcm_router_service_step((runtime_component_t*)&router, 1000);

    TEST_ASSERT_EQUAL(sizeof(rtcm_data), byte_ring_buffer_available(&out1));

    uint8_t read_buf[16] = {0};
    byte_ring_buffer_read(&out1, read_buf, sizeof(rtcm_data));
    TEST_ASSERT_EQUAL_MEMORY(rtcm_data, read_buf, sizeof(rtcm_data));
}

void test_forward_to_multiple_outputs(void)
{
    rtcm_router_add_output(&router, &out1);
    rtcm_router_add_output(&router, &out2);

    uint8_t rtcm_data[] = {0xD3, 0x00, 0x05, 0x01, 0x02};
    byte_ring_buffer_write(&source, rtcm_data, sizeof(rtcm_data));

    rtcm_router_service_step((runtime_component_t*)&router, 1000);

    TEST_ASSERT_EQUAL(sizeof(rtcm_data), byte_ring_buffer_available(&out1));
    TEST_ASSERT_EQUAL(sizeof(rtcm_data), byte_ring_buffer_available(&out2));
}

void test_no_source_no_crash(void)
{
    router.rtcm_source = NULL;
    rtcm_router_service_step((runtime_component_t*)&router, 1000);
    TEST_PASS();
}

void test_no_outputs_data_dropped(void)
{
    /* No outputs registered, source has data */
    uint8_t data[] = {0xD3, 0x00};
    byte_ring_buffer_write(&source, data, sizeof(data));

    rtcm_router_service_step((runtime_component_t*)&router, 1000);

    /* Source should be drained even without outputs */
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&source));

    /* Stats should show bytes_in but no bytes_out */
    const rtcm_stats_t* stats = rtcm_router_get_stats(&router);
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_EQUAL(2, stats->bytes_in);
    TEST_ASSERT_EQUAL(0, stats->bytes_out);
}

void test_output_buffer_full_drops_data(void)
{
    rtcm_router_add_output(&router, &out1);

    /* Fill output buffer almost completely */
    uint8_t fill[127];
    memset(fill, 0xAA, sizeof(fill));
    byte_ring_buffer_write(&out1, fill, sizeof(fill));
    TEST_ASSERT_EQUAL(127, byte_ring_buffer_available(&out1));

    /* Push 5 bytes via source, but only 1 byte fits in output */
    uint8_t rtcm_data[] = {0xD3, 0x00, 0x05, 0x01, 0x02};
    byte_ring_buffer_write(&source, rtcm_data, sizeof(rtcm_data));

    rtcm_router_service_step((runtime_component_t*)&router, 1000);

    /* Only 1 byte should have been written to output (128 - 127 = 1) */
    TEST_ASSERT_EQUAL(128, byte_ring_buffer_available(&out1));

    /* Output should track dropped bytes */
    TEST_ASSERT_TRUE(router.outputs[0].bytes_dropped > 0);
}

/* ---- Stats ---- */

void test_stats_track_correctly(void)
{
    rtcm_router_add_output(&router, &out1);

    uint8_t data1[] = {0xD3, 0x00, 0x03};
    byte_ring_buffer_write(&source, data1, sizeof(data1));
    rtcm_router_service_step((runtime_component_t*)&router, 1000);

    uint8_t data2[] = {0xD3, 0x00, 0x04};
    byte_ring_buffer_write(&source, data2, sizeof(data2));
    rtcm_router_service_step((runtime_component_t*)&router, 2000);

    const rtcm_stats_t* stats = rtcm_router_get_stats(&router);
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_EQUAL(6, stats->bytes_in);
    TEST_ASSERT_EQUAL(6, stats->bytes_out);
    TEST_ASSERT_EQUAL(0, stats->bytes_dropped);
    TEST_ASSERT_EQUAL(2000, stats->last_activity_us);
}

void test_stats_null_returns_null(void)
{
    TEST_ASSERT_NULL(rtcm_router_get_stats(NULL));
}

/* ---- Service step null ---- */

void test_service_step_null_does_not_crash(void)
{
    rtcm_router_service_step(NULL, 1000);
    TEST_PASS();
}

/* ---- NAV-RTCM-001: Both outputs receive identical RTCM bytes ---- */

void test_both_outputs_identical_bytes(void)
{
    rtcm_router_add_output(&router, &out1);
    rtcm_router_add_output(&router, &out2);

    uint8_t rtcm_data[] = {0xD3, 0x00, 0x13, 0x3E, 0xD7, 0x02, 0x01, 0x03, 0x8A, 0x4B};
    byte_ring_buffer_write(&source, rtcm_data, sizeof(rtcm_data));

    rtcm_router_service_step((runtime_component_t*)&router, 1000);

    /* Both should have same number of bytes */
    size_t avail1 = byte_ring_buffer_available(&out1);
    size_t avail2 = byte_ring_buffer_available(&out2);
    TEST_ASSERT_EQUAL(sizeof(rtcm_data), avail1);
    TEST_ASSERT_EQUAL(sizeof(rtcm_data), avail2);

    /* Read and verify byte-for-byte identity */
    uint8_t read1[16] = {0};
    uint8_t read2[16] = {0};
    byte_ring_buffer_read(&out1, read1, sizeof(rtcm_data));
    byte_ring_buffer_read(&out2, read2, sizeof(rtcm_data));
    TEST_ASSERT_EQUAL_MEMORY(rtcm_data, read1, sizeof(rtcm_data));
    TEST_ASSERT_EQUAL_MEMORY(rtcm_data, read2, sizeof(rtcm_data));
    TEST_ASSERT_EQUAL_MEMORY(read1, read2, sizeof(rtcm_data));
}

/* ---- NAV-RTCM-001: Full primary buffer does NOT affect secondary ---- */

void test_full_primary_does_not_affect_secondary(void)
{
    rtcm_router_add_output(&router, &out1);  /* primary — will be filled */
    rtcm_router_add_output(&router, &out2);  /* secondary — has space */

    /* Fill out1 (128 bytes) almost completely — leave 1 byte free */
    uint8_t fill[127];
    memset(fill, 0xAA, sizeof(fill));
    byte_ring_buffer_write(&out1, fill, sizeof(fill));
    TEST_ASSERT_EQUAL(127, byte_ring_buffer_available(&out1));

    /* Push 5 bytes via source */
    uint8_t rtcm_data[] = {0xD3, 0x00, 0x05, 0x01, 0x02};
    byte_ring_buffer_write(&source, rtcm_data, sizeof(rtcm_data));

    rtcm_router_service_step((runtime_component_t*)&router, 1000);

    /* Primary: 128 bytes (127 + 1), 4 bytes dropped */
    TEST_ASSERT_EQUAL(128, byte_ring_buffer_available(&out1));
    TEST_ASSERT_EQUAL(4, router.outputs[0].bytes_dropped);

    /* Secondary: all 5 bytes received */
    TEST_ASSERT_EQUAL(5, byte_ring_buffer_available(&out2));
    TEST_ASSERT_EQUAL(0, router.outputs[1].bytes_dropped);
    TEST_ASSERT_EQUAL(5, router.outputs[1].bytes_forwarded);

    /* Verify secondary byte values */
    uint8_t read2[8] = {0};
    byte_ring_buffer_read(&out2, read2, 5);
    TEST_ASSERT_EQUAL_MEMORY(rtcm_data, read2, 5);
}

/* ---- NAV-RTCM-001: output_overflow_count tracked ---- */

void test_output_overflow_count_increments(void)
{
    rtcm_router_add_output(&router, &out1);

    /* Fill output buffer */
    uint8_t fill[127];
    memset(fill, 0xAA, sizeof(fill));
    byte_ring_buffer_write(&out1, fill, sizeof(fill));

    /* First overflow event: 5 bytes, only 1 fits */
    uint8_t data1[] = {0xD3, 0x00, 0x05, 0x01, 0x02};
    byte_ring_buffer_write(&source, data1, sizeof(data1));
    rtcm_router_service_step((runtime_component_t*)&router, 1000);
    TEST_ASSERT_EQUAL(1, rtcm_router_get_output_overflow_count(&router));

    /* Second overflow event: 3 bytes, 0 fit (buffer now full) */
    uint8_t data2[] = {0xD3, 0x00, 0x03};
    byte_ring_buffer_write(&source, data2, sizeof(data2));
    rtcm_router_service_step((runtime_component_t*)&router, 2000);
    TEST_ASSERT_EQUAL(2, rtcm_router_get_output_overflow_count(&router));

    /* No overflow: drain buffer, write normal data */
    byte_ring_buffer_read(&out1, fill, 128);
    uint8_t data3[] = {0xD3, 0x00, 0x02};
    byte_ring_buffer_write(&source, data3, sizeof(data3));
    rtcm_router_service_step((runtime_component_t*)&router, 3000);
    TEST_ASSERT_EQUAL(2, rtcm_router_get_output_overflow_count(&router));
}

void test_output_overflow_count_null(void)
{
    TEST_ASSERT_EQUAL(0, rtcm_router_get_output_overflow_count(NULL));
}

/* ---- NAV-RTCM-001: Router does NOT call HAL/UART ----
 * This is verified by the source code itself — the router only uses
 * byte_ring_buffer_read/write. No hal_uart or transport_uart includes. */

void test_router_is_generic_no_hal_dependency(void)
{
    /* The router operates purely on ring buffers.
     * This test verifies the stats API works without any HAL setup. */
    rtcm_router_add_output(&router, &out1);

    uint8_t data[] = {0xD3, 0x00, 0x02};
    byte_ring_buffer_write(&source, data, sizeof(data));
    rtcm_router_service_step((runtime_component_t*)&router, 1000);

    const rtcm_stats_t* stats = rtcm_router_get_stats(&router);
    TEST_ASSERT_NOT_NULL(stats);
    TEST_ASSERT_EQUAL(3, stats->bytes_in);
    TEST_ASSERT_EQUAL(3, stats->bytes_out);
    TEST_ASSERT_EQUAL(0, stats->bytes_dropped);
    TEST_ASSERT_EQUAL(1000, stats->last_activity_us);
    TEST_ASSERT_EQUAL(0, rtcm_router_get_output_overflow_count(&router));
}

/* ---- Multi-step forwarding: data accumulated across multiple service steps ---- */

void test_multi_step_accumulates_stats(void)
{
    rtcm_router_add_output(&router, &out1);
    rtcm_router_add_output(&router, &out2);

    /* Step 1: 3 bytes */
    uint8_t d1[] = {0xD3, 0x00, 0x03};
    byte_ring_buffer_write(&source, d1, sizeof(d1));
    rtcm_router_service_step((runtime_component_t*)&router, 1000);

    /* Step 2: 4 bytes */
    uint8_t d2[] = {0xD3, 0x00, 0x04, 0x01};
    byte_ring_buffer_write(&source, d2, sizeof(d2));
    rtcm_router_service_step((runtime_component_t*)&router, 2000);

    /* Step 3: 5 bytes */
    uint8_t d3[] = {0xD3, 0x00, 0x05, 0x01, 0x02};
    byte_ring_buffer_write(&source, d3, sizeof(d3));
    rtcm_router_service_step((runtime_component_t*)&router, 3000);

    /* Total: 12 bytes in, 12 bytes out per output */
    const rtcm_stats_t* stats = rtcm_router_get_stats(&router);
    TEST_ASSERT_EQUAL(12, stats->bytes_in);
    TEST_ASSERT_EQUAL(24, stats->bytes_out);  /* 2 outputs × 12 bytes */
    TEST_ASSERT_EQUAL(0, stats->bytes_dropped);
    TEST_ASSERT_EQUAL(3000, stats->last_activity_us);

    /* Per-output: each should have 12 bytes */
    TEST_ASSERT_EQUAL(12, router.outputs[0].bytes_forwarded);
    TEST_ASSERT_EQUAL(12, router.outputs[1].bytes_forwarded);

    /* Both outputs should have 12 bytes available */
    TEST_ASSERT_EQUAL(12, byte_ring_buffer_available(&out1));
    TEST_ASSERT_EQUAL(12, byte_ring_buffer_available(&out2));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_resets_state);
    RUN_TEST(test_init_null_does_not_crash);
    RUN_TEST(test_add_output_returns_index);
    RUN_TEST(test_add_output_max_exceeded);
    RUN_TEST(test_add_output_null_returns_negative);
    RUN_TEST(test_set_source);
    RUN_TEST(test_set_source_null_does_not_crash);
    RUN_TEST(test_forward_to_single_output);
    RUN_TEST(test_forward_to_multiple_outputs);
    RUN_TEST(test_no_source_no_crash);
    RUN_TEST(test_no_outputs_data_dropped);
    RUN_TEST(test_output_buffer_full_drops_data);
    RUN_TEST(test_stats_track_correctly);
    RUN_TEST(test_stats_null_returns_null);
    RUN_TEST(test_service_step_null_does_not_crash);

    /* NAV-RTCM-001: Dual output tests */
    RUN_TEST(test_both_outputs_identical_bytes);
    RUN_TEST(test_full_primary_does_not_affect_secondary);
    RUN_TEST(test_output_overflow_count_increments);
    RUN_TEST(test_output_overflow_count_null);
    RUN_TEST(test_router_is_generic_no_hal_dependency);
    RUN_TEST(test_multi_step_accumulates_stats);
    UNITY_END();
}
