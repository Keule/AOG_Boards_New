/**
 * test_gnss_um980 — host-side unit tests for the UM980 GNSS receiver component.
 *
 * Tests cover: init, GGA/RMC parsing, checksum validation,
 * fragmented input, and multi-instance independence.
 */

#include "unity.h"
#include "gnss_um980.h"
#include "byte_ring_buffer.h"
#include <stdio.h>
#include <string.h>

/* ---- NMEA checksum helper ---- */

static uint8_t nmea_checksum(const char* sentence)
{
    uint8_t cs = 0;
    for (size_t i = 0; sentence[i] != '\0'; i++) {
        cs ^= (uint8_t)sentence[i];
    }
    return cs;
}

/**
 * Build a complete NMEA sentence string with checksum and \r\n.
 * payload = content between $ and *, e.g. "GNGGA,123519,..."
 * Returns number of bytes written (always <= buf_size).
 */
static size_t make_nmea(char* buf, size_t buf_size, const char* payload)
{
    uint8_t cs = nmea_checksum(payload);
    return (size_t)snprintf(buf, buf_size, "$%s*%02X\r\n", payload, cs);
}

/* ---- Test data ---- */

/* Valid GGA: position near Berlin, GPS fix, 12 sats */
#define GGA_PAYLOAD "GNGGA,092750.000,5243.1234,N,01322.5678,E,1,12,0.9,34.5,M,47.0,M,,"
/* Valid RMC: active, same position */
#define RMC_PAYLOAD "GNRMC,092750.00,A,5243.1234,N,01322.5678,E,0.5,45.3,290424,,,A"
/* Invalid checksum sentence (intentionally wrong) */
#define BAD_PAYLOAD "GNGGA,092750.000,5243.1234,N,01322.5678,E,1,12,0.9,34.5,M,47.0,M,,"

/* ---- Fixtures ---- */

static gnss_um980_t gnss_primary;
static gnss_um980_t gnss_secondary;

static uint8_t primary_rx_storage[256];
static uint8_t secondary_rx_storage[256];
static byte_ring_buffer_t primary_rx;
static byte_ring_buffer_t secondary_rx;

/* ---- Unity callbacks ---- */

void setUp(void)
{
    memset(&gnss_primary,   0, sizeof(gnss_primary));
    memset(&gnss_secondary, 0, sizeof(gnss_secondary));

    gnss_um980_init(&gnss_primary,   0, "gnss_primary");
    gnss_um980_init(&gnss_secondary, 1, "gnss_secondary");

    memset(primary_rx_storage,   0, sizeof(primary_rx_storage));
    memset(secondary_rx_storage, 0, sizeof(secondary_rx_storage));
    byte_ring_buffer_init(&primary_rx,   primary_rx_storage,   sizeof(primary_rx_storage));
    byte_ring_buffer_init(&secondary_rx, secondary_rx_storage, sizeof(secondary_rx_storage));

    gnss_um980_set_rx_source(&gnss_primary,   &primary_rx);
    gnss_um980_set_rx_source(&gnss_secondary, &secondary_rx);
}

void tearDown(void) {}

/* ================================================================
 * Teil 1 – Init
 * ================================================================ */

void test_init_sets_instance_id_and_name(void)
{
    TEST_ASSERT_EQUAL(0, gnss_primary.instance_id);
    TEST_ASSERT_EQUAL_STRING("gnss_primary", gnss_primary.name);

    TEST_ASSERT_EQUAL(1, gnss_secondary.instance_id);
    TEST_ASSERT_EQUAL_STRING("gnss_secondary", gnss_secondary.name);
}

void test_init_sets_parser_and_snapshot(void)
{
    /* Parser should be in IDLE state after init */
    TEST_ASSERT_EQUAL(NMEA_STATE_IDLE, gnss_primary.nmea_parser.state);

    /* Snapshot should be invalid initially */
    TEST_ASSERT_FALSE(snapshot_buffer_is_valid(&gnss_primary.position_snapshot));

    /* No valid data yet */
    TEST_ASSERT_FALSE(gnss_primary.gga_valid);
    TEST_ASSERT_FALSE(gnss_primary.rmc_valid);
    TEST_ASSERT_EQUAL(0, gnss_primary.sentences_parsed);
    TEST_ASSERT_EQUAL(0, gnss_primary.sentences_error);
}

void test_init_null_does_not_crash(void)
{
    gnss_um980_init(NULL, 0, "x");
    gnss_um980_set_rx_source(NULL, NULL);
    TEST_PASS();
}

/* ================================================================
 * Teil 1 – Feed valid GGA → gga_valid + snapshot
 * ================================================================ */

void test_feed_valid_gga_sets_gga_valid_and_updates_snapshot(void)
{
    char sentence[128];
    size_t len = make_nmea(sentence, sizeof(sentence), GGA_PAYLOAD);

    uint32_t parsed = gnss_um980_feed(&gnss_primary, (const uint8_t*)sentence, len);
    TEST_ASSERT_EQUAL(1, parsed);

    TEST_ASSERT_TRUE(gnss_primary.gga_valid);
    TEST_ASSERT_EQUAL(1, gnss_primary.sentences_parsed);
    TEST_ASSERT_EQUAL(0, gnss_primary.sentences_error);

    /* GGA content should be parsed */
    const nmea_gga_t* gga = gnss_um980_get_gga(&gnss_primary);
    TEST_ASSERT_NOT_NULL(gga);
    TEST_ASSERT_EQUAL(1, gga->fix_quality);
    TEST_ASSERT_EQUAL(12, gga->num_sats);

    /* Snapshot should be valid */
    TEST_ASSERT_TRUE(snapshot_buffer_is_valid(&gnss_primary.position_snapshot));
    TEST_ASSERT_EQUAL(1, snapshot_buffer_sequence(&gnss_primary.position_snapshot));
}

/* ================================================================
 * Teil 1 – Feed valid RMC → rmc_valid
 * ================================================================ */

void test_feed_valid_rmc_sets_rmc_valid(void)
{
    char sentence[128];
    size_t len = make_nmea(sentence, sizeof(sentence), RMC_PAYLOAD);

    uint32_t parsed = gnss_um980_feed(&gnss_primary, (const uint8_t*)sentence, len);
    TEST_ASSERT_EQUAL(1, parsed);

    TEST_ASSERT_TRUE(gnss_primary.rmc_valid);
    TEST_ASSERT_EQUAL(1, gnss_primary.sentences_parsed);

    const nmea_rmc_t* rmc = gnss_um980_get_rmc(&gnss_primary);
    TEST_ASSERT_NOT_NULL(rmc);
    TEST_ASSERT_TRUE(rmc->status_valid);
    TEST_ASSERT_EQUAL(29, rmc->date_day);
    TEST_ASSERT_EQUAL(4, rmc->date_month);
    TEST_ASSERT_EQUAL(2024, rmc->date_year);
}

/* ================================================================
 * Teil 1 – Invalid checksum → sentences_error
 * ================================================================ */

void test_feed_invalid_checksum_increments_errors(void)
{
    /* Deliberately wrong checksum: *FF instead of the correct value */
    char sentence[128];
    snprintf(sentence, sizeof(sentence), "$%s*FF\r\n", BAD_PAYLOAD);

    gnss_um980_feed(&gnss_primary, (const uint8_t*)sentence, strlen(sentence));

    TEST_ASSERT_EQUAL(0, gnss_primary.sentences_parsed);
    TEST_ASSERT_EQUAL(1, gnss_primary.sentences_error);
    TEST_ASSERT_FALSE(gnss_primary.gga_valid);
}

/* ================================================================
 * Teil 1 – Fragmentierte Eingabe
 * ================================================================ */

void test_fragmented_input_still_parses(void)
{
    char sentence[128];
    size_t total = make_nmea(sentence, sizeof(sentence), GGA_PAYLOAD);

    /* Feed one byte at a time */
    for (size_t i = 0; i < total; i++) {
        gnss_um980_feed(&gnss_primary, (const uint8_t*)&sentence[i], 1);
    }

    TEST_ASSERT_EQUAL(1, gnss_primary.sentences_parsed);
    TEST_ASSERT_TRUE(gnss_primary.gga_valid);

    const nmea_gga_t* gga = gnss_um980_get_gga(&gnss_primary);
    TEST_ASSERT_NOT_NULL(gga);
    TEST_ASSERT_EQUAL(1, gga->fix_quality);
}

void test_two_chunks_parse_correctly(void)
{
    char sentence[128];
    size_t total = make_nmea(sentence, sizeof(sentence), GGA_PAYLOAD);

    /* Split roughly in half */
    size_t half = total / 2;
    gnss_um980_feed(&gnss_primary, (const uint8_t*)sentence, half);
    gnss_um980_feed(&gnss_primary, (const uint8_t*)sentence + half, total - half);

    TEST_ASSERT_EQUAL(1, gnss_primary.sentences_parsed);
    TEST_ASSERT_TRUE(gnss_primary.gga_valid);
}

/* ================================================================
 * Teil 1 – Zwei Instanzen bleiben unabhängig
 * ================================================================ */

void test_two_instances_are_independent(void)
{
    char gga_primary[128];
    char gga_secondary[128];

    /* Primary: GPS fix, 12 sats */
    size_t len1 = make_nmea(gga_primary, sizeof(gga_primary),
        "GNGGA,092750.000,5243.1234,N,01322.5678,E,1,12,0.9,34.5,M,47.0,M,,");

    /* Secondary: DGPS fix, 8 sats */
    size_t len2 = make_nmea(gga_secondary, sizeof(gga_secondary),
        "GNGGA,092751.000,4812.0000,N,01134.0000,E,2,08,1.5,520.0,M,458.0,M,,");

    gnss_um980_feed(&gnss_primary,   (const uint8_t*)gga_primary,   len1);
    gnss_um980_feed(&gnss_secondary, (const uint8_t*)gga_secondary, len2);

    /* Both valid */
    TEST_ASSERT_TRUE(gnss_primary.gga_valid);
    TEST_ASSERT_TRUE(gnss_secondary.gga_valid);

    /* Different fix quality */
    TEST_ASSERT_EQUAL(1, gnss_um980_get_gga(&gnss_primary)->fix_quality);
    TEST_ASSERT_EQUAL(2, gnss_um980_get_gga(&gnss_secondary)->fix_quality);

    /* Different sat count */
    TEST_ASSERT_EQUAL(12, gnss_um980_get_gga(&gnss_primary)->num_sats);
    TEST_ASSERT_EQUAL(8,  gnss_um980_get_gga(&gnss_secondary)->num_sats);

    /* Different lat */
    TEST_ASSERT(gnss_um980_get_gga(&gnss_primary)->latitude   != gnss_um980_get_gga(&gnss_secondary)->latitude);

    /* Error counters independent */
    gnss_um980_feed(&gnss_primary, (const uint8_t*)"$GNGGA,xxx*FF\r\n", 14);
    TEST_ASSERT_EQUAL(1, gnss_primary.sentences_error);
    TEST_ASSERT_EQUAL(0, gnss_secondary.sentences_error);
}

/* ================================================================
 * Service step via ring buffer
 * ================================================================ */

void test_service_step_reads_from_rx_source(void)
{
    char sentence[128];
    size_t len = make_nmea(sentence, sizeof(sentence), GGA_PAYLOAD);

    /* Write NMEA data into the RX ring buffer */
    byte_ring_buffer_write(&primary_rx, (const uint8_t*)sentence, len);

    /* service_step reads in 64-byte chunks (matches production buffer size).
     * Call twice to ensure the full 72-byte sentence is consumed. */
    gnss_um980_service_step((runtime_component_t*)&gnss_primary, 1000000);
    gnss_um980_service_step((runtime_component_t*)&gnss_primary, 2000000);

    TEST_ASSERT_TRUE(gnss_primary.gga_valid);
    TEST_ASSERT_EQUAL(1, gnss_primary.sentences_parsed);
    TEST_ASSERT_EQUAL(0, byte_ring_buffer_available(&primary_rx));
}

void test_service_step_null_does_not_crash(void)
{
    gnss_um980_service_step(NULL, 1000);
    TEST_PASS();
}

/* ---- Runner ---- */

int main(void)
{
    UNITY_BEGIN();

    /* Init */
    RUN_TEST(test_init_sets_instance_id_and_name);
    RUN_TEST(test_init_sets_parser_and_snapshot);
    RUN_TEST(test_init_null_does_not_crash);

    /* GGA */
    RUN_TEST(test_feed_valid_gga_sets_gga_valid_and_updates_snapshot);

    /* RMC */
    RUN_TEST(test_feed_valid_rmc_sets_rmc_valid);

    /* Checksum */
    RUN_TEST(test_feed_invalid_checksum_increments_errors);

    /* Fragmented */
    RUN_TEST(test_fragmented_input_still_parses);
    RUN_TEST(test_two_chunks_parse_correctly);

    /* Independence */
    RUN_TEST(test_two_instances_are_independent);

    /* Service step */
    RUN_TEST(test_service_step_reads_from_rx_source);
    RUN_TEST(test_service_step_null_does_not_crash);

    return UNITY_END();
}
