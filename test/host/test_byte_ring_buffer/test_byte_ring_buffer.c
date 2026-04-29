#include "unity.h"
#include "byte_ring_buffer.h"
#include <string.h>

static uint8_t storage[64];
static byte_ring_buffer_t buf;

void setUp(void)
{
    memset(storage, 0, sizeof(storage));
    memset(&buf, 0, sizeof(buf));
    byte_ring_buffer_init(&buf, storage, sizeof(storage));
}

void tearDown(void) {}

/* ---- Init ---- */

void test_init_sets_capacity_and_zeros_counters(void)
{
    TEST_ASSERT_EQUAL_PTR(storage, buf.data);
    TEST_ASSERT_EQUAL(64, buf.capacity);
    TEST_ASSERT_EQUAL(0, buf.size);
    TEST_ASSERT_EQUAL(0, buf.head);
    TEST_ASSERT_EQUAL(0, buf.tail);
    TEST_ASSERT_EQUAL(0, buf.overflow_count);
}

void test_init_with_null_pointer_does_not_crash(void)
{
    byte_ring_buffer_init(NULL, storage, sizeof(storage));
    /* Must not crash — that is the test */
    TEST_PASS();
}

/* ---- Write ---- */

void test_write_single_byte(void)
{
    uint8_t byte = 0x42;
    size_t written = byte_ring_buffer_write(&buf, &byte, 1);
    TEST_ASSERT_EQUAL(1, written);
    TEST_ASSERT_EQUAL(1, buf.size);
}

void test_write_multiple_bytes(void)
{
    uint8_t data[] = {1, 2, 3, 4, 5};
    size_t written = byte_ring_buffer_write(&buf, data, 5);
    TEST_ASSERT_EQUAL(5, written);
    TEST_ASSERT_EQUAL(5, buf.size);
}

void test_write_fills_to_capacity(void)
{
    uint8_t data[64];
    memset(data, 0xAA, sizeof(data));
    size_t written = byte_ring_buffer_write(&buf, data, 64);
    TEST_ASSERT_EQUAL(64, written);
    TEST_ASSERT_EQUAL(64, buf.size);
}

void test_write_beyond_capacity_triggers_overflow(void)
{
    uint8_t data[70];
    memset(data, 0xBB, sizeof(data));
    size_t written = byte_ring_buffer_write(&buf, data, 70);
    TEST_ASSERT_EQUAL(64, written);
    TEST_ASSERT_EQUAL(1, buf.overflow_count);
}

void test_write_null_buffer_returns_zero(void)
{
    size_t written = byte_ring_buffer_write(NULL, (uint8_t[]){0}, 1);
    TEST_ASSERT_EQUAL(0, written);
}

void test_write_null_data_returns_zero(void)
{
    size_t written = byte_ring_buffer_write(&buf, NULL, 1);
    TEST_ASSERT_EQUAL(0, written);
}

/* ---- Read ---- */

void test_read_single_byte(void)
{
    uint8_t data[] = {0x42};
    byte_ring_buffer_write(&buf, data, 1);

    uint8_t out = 0;
    size_t read = byte_ring_buffer_read(&buf, &out, 1);
    TEST_ASSERT_EQUAL(1, read);
    TEST_ASSERT_EQUAL(0x42, out);
    TEST_ASSERT_EQUAL(0, buf.size);
}

void test_read_multiple_bytes_fifo_order(void)
{
    uint8_t data[] = {10, 20, 30};
    byte_ring_buffer_write(&buf, data, 3);

    uint8_t out[3] = {0};
    size_t read = byte_ring_buffer_read(&buf, out, 3);
    TEST_ASSERT_EQUAL(3, read);
    TEST_ASSERT_EQUAL(10, out[0]);
    TEST_ASSERT_EQUAL(20, out[1]);
    TEST_ASSERT_EQUAL(30, out[2]);
}

void test_read_more_than_available(void)
{
    uint8_t data[] = {1, 2};
    byte_ring_buffer_write(&buf, data, 2);

    uint8_t out[10] = {0};
    size_t read = byte_ring_buffer_read(&buf, out, 10);
    TEST_ASSERT_EQUAL(2, read);
}

void test_read_empty_buffer_returns_zero(void)
{
    uint8_t out = 0;
    size_t read = byte_ring_buffer_read(&buf, &out, 1);
    TEST_ASSERT_EQUAL(0, read);
}

void test_read_null_buffer_returns_zero(void)
{
    size_t read = byte_ring_buffer_read(NULL, (uint8_t[]){0}, 1);
    TEST_ASSERT_EQUAL(0, read);
}

/* ---- Wrap-around (ring behavior) ---- */

void test_wraparound_write_read_cycle(void)
{
    uint8_t storage_small[4];
    byte_ring_buffer_t small;
    byte_ring_buffer_init(&small, storage_small, 4);

    /* Write 3, read 3, write 2, read 2 — tests head/tail wrapping */
    uint8_t in1[] = {1, 2, 3};
    byte_ring_buffer_write(&small, in1, 3);

    uint8_t out1[3] = {0};
    byte_ring_buffer_read(&small, out1, 3);
    TEST_ASSERT_EQUAL(1, out1[0]);
    TEST_ASSERT_EQUAL(2, out1[1]);
    TEST_ASSERT_EQUAL(3, out1[2]);

    uint8_t in2[] = {4, 5};
    byte_ring_buffer_write(&small, in2, 2);

    uint8_t out2[2] = {0};
    byte_ring_buffer_read(&small, out2, 2);
    TEST_ASSERT_EQUAL(4, out2[0]);
    TEST_ASSERT_EQUAL(5, out2[1]);
}

void test_wraparound_full_cycle(void)
{
    uint8_t storage_small[8];
    byte_ring_buffer_t small;
    byte_ring_buffer_init(&small, storage_small, 8);

    /* Multiple write/read cycles to exercise wrapping multiple times */
    for (int round = 0; round < 5; round++) {
        uint8_t in[6] = {round, round+1, round+2, round+3, round+4, round+5};
        byte_ring_buffer_write(&small, in, 6);

        uint8_t out[6] = {0};
        byte_ring_buffer_read(&small, out, 6);
        TEST_ASSERT_EQUAL(round, out[0]);
        TEST_ASSERT_EQUAL(round + 5, out[5]);
    }
}

/* ---- Available ---- */

void test_available_matches_size(void)
{
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&buf));

    uint8_t data[] = {1, 2, 3};
    byte_ring_buffer_write(&buf, data, 3);
    TEST_ASSERT_EQUAL(3, byte_ring_buffer_available(&buf));

    uint8_t out[1];
    byte_ring_buffer_read(&buf, out, 1);
    TEST_ASSERT_EQUAL(2, byte_ring_buffer_available(&buf));
}

void test_available_null_buffer_returns_zero(void)
{
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(NULL));
}

/* ---- Overflow counter ---- */

void test_overflow_count_increments(void)
{
    uint8_t data[100];
    memset(data, 0, sizeof(data));
    byte_ring_buffer_write(&buf, data, 60);  /* fills most of 64 */

    uint8_t more[20];
    byte_ring_buffer_write(&buf, more, 20);  /* tries to write 20, only 4 fit */

    TEST_ASSERT_EQUAL(64, buf.size);
    TEST_ASSERT_EQUAL(1, buf.overflow_count);

    byte_ring_buffer_write(&buf, more, 1);   /* buffer full, overflow again */
    TEST_ASSERT_EQUAL(2, buf.overflow_count);
}

void test_overflow_count_null_buffer_returns_zero(void)
{
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_overflow_count(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_sets_capacity_and_zeros_counters);
    RUN_TEST(test_init_with_null_pointer_does_not_crash);
    RUN_TEST(test_write_single_byte);
    RUN_TEST(test_write_multiple_bytes);
    RUN_TEST(test_write_fills_to_capacity);
    RUN_TEST(test_write_beyond_capacity_triggers_overflow);
    RUN_TEST(test_write_null_buffer_returns_zero);
    RUN_TEST(test_write_null_data_returns_zero);
    RUN_TEST(test_read_single_byte);
    RUN_TEST(test_read_multiple_bytes_fifo_order);
    RUN_TEST(test_read_more_than_available);
    RUN_TEST(test_read_empty_buffer_returns_zero);
    RUN_TEST(test_read_null_buffer_returns_zero);
    RUN_TEST(test_wraparound_write_read_cycle);
    RUN_TEST(test_wraparound_full_cycle);
    RUN_TEST(test_available_matches_size);
    RUN_TEST(test_available_null_buffer_returns_zero);
    RUN_TEST(test_overflow_count_increments);
    RUN_TEST(test_overflow_count_null_buffer_returns_zero);
    return UNITY_END();
}
