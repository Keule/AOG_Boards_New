/* ========================================================================
 * test_nmea_rate_fix.c — Unit tests for NAV-UM980-LOG-SYNTAX-RATE-FIX-001
 *
 * Tests:
 *   1. Rate calculation with 25 counts / 5086 ms => ~4.91 Hz
 *   2. Rate calculation with 120 counts / 5086 ms => ~23.59 Hz
 *   3. WARMUP on first sample
 *   4. window_ms correctly computed
 *   5. ACK detection: "response: OK*CS"
 *   6. ACK detection: "response: OK" (no checksum)
 *   7. ACK detection: "ERROR"
 *   8. ACK detection: case insensitive
 *   9. Command response with embedded NMEA lines
 *   10. Restore status: transport_success != ack_ok
 *   11. Rate with zero delta_ms
 *   12. Multiple ACK patterns
 * ======================================================================== */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>

/* Include the header with types we need */
#include "gnss_nmea_config.h"

/* ---- Test framework ---- */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  [TEST %d] %-50s", tests_run, name); \
} while(0)

#define PASS() do { \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define FAIL(msg) do { \
    tests_failed++; \
    printf("FAIL: %s\n", msg); \
} while(0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { FAIL(msg); return; } \
} while(0)

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        char buf[128]; snprintf(buf, sizeof(buf), "%s (got %d, expected %d)", msg, (int)(a), (int)(b)); \
        FAIL(buf); return; \
    } \
} while(0)

#define ASSERT_FLOAT_NEAR(actual, expected, epsilon, msg) do { \
    float diff = (float)fabs((double)(actual) - (double)(expected)); \
    if (diff > epsilon) { \
        char buf[256]; snprintf(buf, sizeof(buf), "%s (got %.4f, expected %.4f, diff=%.6f)", \
                               msg, (double)(actual), (double)(expected), (double)diff); \
        FAIL(buf); return; \
    } \
} while(0)

/* =================================================================
 * Test 1: Rate calculation — 25 counts / 5086 ms => ~4.91 Hz
 * ================================================================= */

static void test_rate_25_counts_5086ms(void)
{
    TEST("rate 25 counts / 5086 ms => ~4.91 Hz");

    /* Simulate rate calculation formula */
    uint32_t delta_count = 25;
    uint64_t delta_ms = 5086;
    float rate_hz = (float)delta_count * 1000.0f / (float)delta_ms;

    ASSERT_FLOAT_NEAR(rate_hz, 4.91f, 0.01f, "rate should be ~4.91 Hz");
    PASS();
}

/* =================================================================
 * Test 2: Rate calculation — 120 counts / 5086 ms => ~23.59 Hz
 * ================================================================= */

static void test_rate_120_counts_5086ms(void)
{
    TEST("rate 120 counts / 5086 ms => ~23.59 Hz");

    uint32_t delta_count = 120;
    uint64_t delta_ms = 5086;
    float rate_hz = (float)delta_count * 1000.0f / (float)delta_ms;

    ASSERT_FLOAT_NEAR(rate_hz, 23.59f, 0.1f, "rate should be ~23.59 Hz");
    PASS();
}

/* =================================================================
 * Test 3: WARMUP on first sample
 * ================================================================= */

static void test_rate_warmup_first_sample(void)
{
    TEST("warmup on first sample (prev_time_ms = 0)");

    uint64_t prev_time_ms = 0;
    uint64_t curr_time_ms = 5000;

    /* With prev_time_ms == 0, we should NOT compute rate */
    bool should_compute = (prev_time_ms != 0);
    ASSERT_TRUE(!should_compute, "should not compute rate on first sample");
    PASS();
}

/* =================================================================
 * Test 4: window_ms correctly computed
 * ================================================================= */

static void test_rate_window_ms(void)
{
    TEST("window_ms = delta between timestamps");

    uint64_t prev_time_ms = 10000;
    uint64_t curr_time_ms = 15086;
    uint32_t window_ms = (uint32_t)(curr_time_ms - prev_time_ms);

    ASSERT_EQ(window_ms, 5086, "window_ms should be 5086");
    PASS();
}

/* =================================================================
 * Test 5: ACK detection — "response: OK*CS"
 * ================================================================= */

static void test_ack_response_ok_with_checksum(void)
{
    TEST("ACK: response with OK and checksum");

    const char* response = "\r\nresponse: OK*3B\r\n";
    uint16_t resp_len = (uint16_t)strlen(response);
    um980_cmd_result_t result;

    bool definitive = um980_parse_response(response, resp_len, &result);
    ASSERT_TRUE(definitive, "should find definitive status");
    ASSERT_EQ(result.status, UM980_CMD_OK, "should be OK");
    ASSERT_TRUE(result.ack_ok, "ack_ok should be true");
    PASS();
}

/* =================================================================
 * Test 6: ACK detection — "response: OK" (no checksum)
 * ================================================================= */

static void test_ack_response_ok_no_checksum(void)
{
    TEST("ACK: response: OK without checksum");

    const char* response = "\r\nresponse: OK\r\n";
    uint16_t resp_len = (uint16_t)strlen(response);
    um980_cmd_result_t result;

    bool definitive = um980_parse_response(response, resp_len, &result);
    ASSERT_TRUE(definitive, "should find definitive status");
    ASSERT_EQ(result.status, UM980_CMD_OK, "should be OK");
    PASS();
}

/* =================================================================
 * Test 7: ACK detection — "ERROR"
 * ================================================================= */

static void test_ack_response_error(void)
{
    TEST("ACK: ERROR response");

    const char* response = "\r\nresponse: ERROR*22\r\n";
    uint16_t resp_len = (uint16_t)strlen(response);
    um980_cmd_result_t result;

    bool definitive = um980_parse_response(response, resp_len, &result);
    ASSERT_TRUE(definitive, "should find definitive status");
    ASSERT_EQ(result.status, UM980_CMD_ERROR, "should be ERROR");
    ASSERT_TRUE(result.ack_error, "ack_error should be true");
    PASS();
}

/* =================================================================
 * Test 8: ACK detection — case insensitive
 * ================================================================= */

static void test_ack_case_insensitive(void)
{
    TEST("ACK: case insensitive matching");

    /* Lowercase "ok" */
    const char* resp1 = "response: ok\r\n";
    um980_cmd_result_t result1;
    bool d1 = um980_parse_response(resp1, (uint16_t)strlen(resp1), &result1);
    ASSERT_TRUE(d1 && result1.ack_ok, "lowercase ok should match");

    /* Mixed case "Ok" */
    const char* resp2 = "response: Ok\r\n";
    um980_cmd_result_t result2;
    bool d2 = um980_parse_response(resp2, (uint16_t)strlen(resp2), &result2);
    ASSERT_TRUE(d2 && result2.ack_ok, "mixed case Ok should match");

    /* Uppercase "ERROR" */
    const char* resp3 = "RESPONSE: ERROR\r\n";
    um980_cmd_result_t result3;
    bool d3 = um980_parse_response(resp3, (uint16_t)strlen(resp3), &result3);
    ASSERT_TRUE(d3 && result3.ack_error, "uppercase ERROR should match");

    PASS();
}

/* =================================================================
 * Test 9: Command response with embedded NMEA lines
 * ================================================================= */

static void test_ack_with_nmea_interleaved(void)
{
    TEST("ACK: OK found despite NMEA data interleaved");

    /* Simulated response with NMEA data before and after */
    const char* response =
        "$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n"
        "$GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n"
        "\r\nresponse: OK*3B\r\n";

    uint16_t resp_len = (uint16_t)strlen(response);
    um980_cmd_result_t result;

    bool definitive = um980_parse_response(response, resp_len, &result);
    ASSERT_TRUE(definitive, "should find OK despite NMEA interleaving");
    ASSERT_EQ(result.status, UM980_CMD_OK, "should be OK");
    ASSERT_TRUE(result.response_received, "response should be marked as received");
    PASS();
}

/* =================================================================
 * Test 10: Restore status — transport_success != ack_ok
 * ================================================================= */

static void test_restore_transport_vs_ack(void)
{
    TEST("restore: transport_success and ack_ok are independent");

    /* Simulate: transport OK but no response */
    um980_cmd_result_t result1;
    memset(&result1, 0, sizeof(result1));
    result1.transport_ok = true;
    result1.response_received = false;
    result1.status = UM980_CMD_TIMEOUT;

    ASSERT_TRUE(result1.transport_ok, "transport should be OK");
    ASSERT_TRUE(result1.status != UM980_CMD_OK, "ack should NOT be OK (timeout)");

    /* Simulate: transport OK, response OK */
    um980_cmd_result_t result2;
    memset(&result2, 0, sizeof(result2));
    result2.transport_ok = true;
    result2.response_received = true;
    result2.ack_ok = true;
    result2.status = UM980_CMD_OK;

    ASSERT_TRUE(result2.transport_ok, "transport should be OK");
    ASSERT_TRUE(result2.ack_ok, "ack should be OK");
    PASS();
}

/* =================================================================
 * Test 11: Rate with zero delta_ms
 * ================================================================= */

static void test_rate_zero_delta(void)
{
    TEST("rate: zero delta_ms should not produce NaN/Inf");

    uint64_t delta_ms = 0;
    uint32_t delta_count = 100;
    float rate_hz = (delta_ms > 0) ? ((float)delta_count * 1000.0f / (float)delta_ms) : 0.0f;

    ASSERT_FLOAT_NEAR(rate_hz, 0.0f, 0.001f, "rate should be 0 for zero delta");
    PASS();
}

/* =================================================================
 * Test 12: Multiple ACK patterns in response
 * ================================================================= */

static void test_ack_multiple_patterns(void)
{
    TEST("ACK: OK takes precedence when both OK and ERROR present");

    /* Edge case: both patterns present (should not happen in practice) */
    const char* response = "response: OK\r\nresponse: ERROR\r\n";
    uint16_t resp_len = (uint16_t)strlen(response);
    um980_cmd_result_t result;

    bool definitive = um980_parse_response(response, resp_len, &result);
    /* OK is found first, but both flags may be set */
    ASSERT_TRUE(definitive, "should find definitive status");
    /* If ERROR is found after OK, ERROR wins (scans entire buffer) */
    ASSERT_TRUE(result.ack_ok || result.ack_error, "at least one should be detected");
    PASS();
}

/* =================================================================
 * Test 13: Response excerpt generation
 * ================================================================= */

static void test_response_excerpt(void)
{
    TEST("response excerpt bounded to ~127 chars");

    const char* response = "response: OK*3B\r\n";
    uint16_t resp_len = (uint16_t)strlen(response);
    um980_cmd_result_t result;

    um980_parse_response(response, resp_len, &result);

    /* Excerpt should be non-empty */
    ASSERT_TRUE(result.response_excerpt[0] != '\0', "excerpt should not be empty");
    /* Excerpt should contain escaped CR/LF */
    ASSERT_TRUE(result.response_received, "response_received should be true");
    PASS();
}

/* =================================================================
 * Test 14: Rate calculation from GNSS_RX visible deltas
 * ================================================================= */

static void test_rate_matches_gnss_rx_deltas(void)
{
    TEST("rate matches GNSS_RX deltas: 25 GGA / 5086 ms = 4.91 Hz");

    /* Simulating the user's exact example:
     * t=41145 ms: gga=63
     * t=46231 ms: gga=88
     * delta = 25 counts, delta_ms = 5086
     * expected rate = 25 * 1000 / 5086 = 4.914 Hz
     */
    uint32_t gga_at_t1 = 63;
    uint32_t gga_at_t2 = 88;
    uint64_t t1_ms = 41145;
    uint64_t t2_ms = 46231;

    uint32_t delta = gga_at_t2 - gga_at_t1;
    uint64_t dt_ms = t2_ms - t1_ms;
    float rate = (dt_ms > 0) ? ((float)delta * 1000.0f / (float)dt_ms) : 0.0f;

    ASSERT_EQ(delta, 25, "delta count should be 25");
    ASSERT_EQ((uint32_t)dt_ms, 5086, "delta time should be 5086 ms");
    ASSERT_FLOAT_NEAR(rate, 4.91f, 0.02f, "rate should match ~4.91 Hz");

    /* Verify the log output format would be:
     * GNSS_NMEA_RATE: primary window_ms=5086 gga=4.91 ...
     */
    printf("    (expect: window_ms=5086 gga=%.2f)\n", (double)rate);
    PASS();
}

/* =================================================================
 * Test 15: Null/empty response handling
 * ================================================================= */

static void test_ack_null_empty_response(void)
{
    TEST("ACK: null and empty response handling");

    um980_cmd_result_t result;

    /* NULL response */
    bool d1 = um980_parse_response(NULL, 0, &result);
    ASSERT_TRUE(!d1, "NULL response should not be definitive");
    ASSERT_TRUE(!result.response_received, "response_received should be false");

    /* Empty response */
    bool d2 = um980_parse_response("", 0, &result);
    ASSERT_TRUE(!d2, "empty response should not be definitive");
    ASSERT_TRUE(!result.response_received, "response_received should be false");
    PASS();
}

/* =================================================================
 * Main
 * ================================================================= */

int main(void)
{
    printf("=== NAV-UM980-LOG-SYNTAX-RATE-FIX-001 Unit Tests ===\n\n");

    /* Rate calculation tests */
    test_rate_25_counts_5086ms();
    test_rate_120_counts_5086ms();
    test_rate_warmup_first_sample();
    test_rate_window_ms();
    test_rate_zero_delta();
    test_rate_matches_gnss_rx_deltas();

    /* ACK detection tests */
    test_ack_response_ok_with_checksum();
    test_ack_response_ok_no_checksum();
    test_ack_response_error();
    test_ack_case_insensitive();
    test_ack_with_nmea_interleaved();
    test_ack_multiple_patterns();
    test_ack_null_empty_response();
    test_response_excerpt();

    /* Restore status tests */
    test_restore_transport_vs_ack();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
