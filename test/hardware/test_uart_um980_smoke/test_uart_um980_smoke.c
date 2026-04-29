/**
 * test_uart_um980_smoke — Hardware smoke test for UART + UM980 GNSS receiver.
 *
 * This test runs on-target (ESP32) and verifies:
 * 1. UART initialization succeeds for both GNSS ports
 * 2. RX data arrives from UM980 (NMEA sentences)
 * 3. TX buffer can accept RTCM correction data
 * 4. No crashes during sustained operation
 *
 * ──────────────────────────────────────────────────────────────
 * Wiring (LilyGO T-Eth Lite ESP32):
 *
 *   UM980 Primary   TX  →  GPIO32  (ESP32 RX1)
 *   UM980 Primary   RX  ←  GPIO33  (ESP32 TX1)
 *   UM980 Secondary TX  →  GPIO2   (ESP32 RX2)
 *   UM980 Secondary RX  ←  GPIO4   (ESP32 TX2)
 *
 *   UM980 VCC   →  3.3V
 *   UM980 GND   →  GND
 *   Baudrate: 921600 (8N1, no flow control)
 * ──────────────────────────────────────────────────────────────
 *
 * Expected NMEA output from UM980 (at 921600 baud):
 *
 *   $GNGGA,<time>,<lat>,N,<lon>,E,<fix>,<sats>,<HDOP>,<alt>,M,<sep>,M,,*<cs>\r\n
 *   $GNRMC,<time>,A,<lat>,N,<lon>,E,<spd>,<hdg>,<date>,,,A*<cs>\r\n
 *   $GNGSA,A,3,<sat_ids>,1.0,0.9,0.8*<cs>\r\n
 *   $GPGSV,<msg>,<total>,<sats>...*<cs>\r\n
 *   $BDGSV,<msg>,<total>,<sats>...*<cs>\r\n
 *
 * Where <cs> is a 2-digit hex checksum (e.g. "4A").
 *
 * ──────────────────────────────────────────────────────────────
 * Build (requires ESP32 target with physical hardware):
 *
 *   pio test -e nav_esp32_t_eth_lite -f "hardware/*"
 *
 * Note: This test requires physical hardware. It cannot run in
 * native mode (pio test -e native).
 * ──────────────────────────────────────────────────────────────
 */

#include "unity.h"

/* TODO: Include ESP-IDF and project headers when building for target:
 * #include "transport_uart.h"
 * #include "hal_uart.h"
 * #include "board_profile.h"
 * #include "gnss_um980.h"
 *
 * #include "esp_log.h"
 * #include "freertos/FreeRTOS.h"
 * #include "freertos/task.h"
 */

/* ---- Test configuration ---- */

#define TEST_UART_BAUDRATE        921600
#define TEST_RX_WAIT_TIMEOUT_MS   3000   /* 3 seconds to receive NMEA */
#define TEST_SUSTAINED_CYCLES     100    /* service_step iterations for stability check */
#define TEST_RTCM_TEST_DATA_LEN   64     /* dummy RTCM bytes to test TX path */

/* ---- Static instances ---- */

/* TODO: Declare transport UART instances for primary and secondary GNSS:
 * static transport_uart_t uart_primary;
 * static transport_uart_t uart_secondary;
 */

/* ---- Unity callbacks ---- */

void setUp(void)
{
    /* TODO: Initialize HAL UART ops
     * hal_uart_init(hal_uart_esp32_ops());
     */

    /* TODO: Initialize transport UART for both GNSS ports
     * transport_uart_config_t cfg_primary = {
     *     .port = BOARD_UART_GNSS_PRIMARY,
     *     .baudrate = TEST_UART_BAUDRATE
     * };
     * TEST_ASSERT_EQUAL(HAL_OK, transport_uart_init(&uart_primary, &cfg_primary));
     *
     * transport_uart_config_t cfg_secondary = {
     *     .port = BOARD_UART_GNSS_SECONDARY,
     *     .baudrate = TEST_UART_BAUDRATE
     * };
     * TEST_ASSERT_EQUAL(HAL_OK, transport_uart_init(&uart_secondary, &cfg_secondary));
     */
}

void tearDown(void)
{
    /* TODO: Deinitialize transport UART and HAL if needed
     * (void)uart_primary;
     * (void)uart_secondary;
     */
}

/* ================================================================
 * Test 1: UART initialization for both GNSS ports
 * ================================================================ */

void test_uart_init_both_ports(void)
{
    /* TODO: Verify both UART ports initialized successfully
     *
     * TEST_ASSERT_EQUAL(BOARD_UART_GNSS_PRIMARY, uart_primary.port);
     * TEST_ASSERT_EQUAL(TEST_UART_BAUDRATE, uart_primary.baudrate);
     * TEST_ASSERT_EQUAL(BOARD_UART_GNSS_SECONDARY, uart_secondary.port);
     * TEST_ASSERT_EQUAL(TEST_UART_BAUDRATE, uart_secondary.baudrate);
     *
     * ESP_LOGI("SMOKE", "Both UART ports initialized at %lu baud", TEST_UART_BAUDRATE);
     */
    TEST_IGNORE_MESSAGE("Requires physical hardware — implement for on-target build");
}

/* ================================================================
 * Test 2: RX data arrives from UM980 (NMEA sentences)
 * ================================================================ */

void test_rx_receives_nmea(void)
{
    /* TODO: Wait for NMEA data from UM980 primary
     *
     * Steps:
     * 1. Call transport_uart_service_step() in a loop with vTaskDelay(10/portTICK_PERIOD_MS)
     * 2. Check transport_uart_rx_available() for data
     * 3. Read data and verify it starts with '$' (NMEA start character)
     * 4. Verify checksum format (two hex digits after '*')
     * 5. Repeat for secondary UM980
     *
     * Example implementation:
     *   uint8_t rx_buf[256];
     *   uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;
     *   while (transport_uart_rx_available(&uart_primary) == 0) {
     *       transport_uart_service_step((runtime_component_t*)&uart_primary,
     *                                    esp_timer_get_time());
     *       vTaskDelay(pdMS_TO_TICKS(10));
     *       if ((xTaskGetTickCount() * portTICK_PERIOD_MS - start) > TEST_RX_WAIT_TIMEOUT_MS) {
     *           TEST_FAIL_MESSAGE("Timeout waiting for NMEA data from primary UM980");
     *           break;
     *       }
     *   }
     *
     *   size_t n = transport_uart_rx_read(&uart_primary, rx_buf, sizeof(rx_buf) - 1);
     *   rx_buf[n] = '\0';
     *   TEST_ASSERT_GREATER_THAN(0, n);
     *   TEST_ASSERT_EQUAL_CHAR('$', (char)rx_buf[0]);
     *   ESP_LOGI("SMOKE", "Primary RX: %u bytes, starts with: %.40s", n, rx_buf);
     */
    TEST_IGNORE_MESSAGE("Requires physical hardware — implement for on-target build");
}

/* ================================================================
 * Test 3: TX buffer can accept RTCM correction data
 * ================================================================ */

void test_tx_accepts_rtcm(void)
{
    /* TODO: Write dummy RTCM data into TX buffer
     *
     * Steps:
     * 1. Generate a dummy RTCM message (header byte 0xD3, length, content)
     * 2. Write to transport_uart_tx_write()
     * 3. Verify written bytes match expected length
     * 4. Call service_step to drain TX buffer (data sent to UM980)
     * 5. Verify tx_free returns full capacity after drain
     *
     * Example implementation:
     *   uint8_t rtcm[TEST_RTCM_TEST_DATA_LEN];
     *   rtcm[0] = 0xD3;  // RTCM3 framing byte
     *   memset(&rtcm[1], 0xAA, sizeof(rtcm) - 1);
     *
     *   size_t written = transport_uart_tx_write(&uart_primary, rtcm, sizeof(rtcm));
     *   TEST_ASSERT_EQUAL(TEST_RTCM_TEST_DATA_LEN, written);
     *   TEST_ASSERT_EQUAL(TRANSPORT_UART_TX_BUFFER_SIZE - TEST_RTCM_TEST_DATA_LEN,
     *                     transport_uart_tx_free(&uart_primary));
     *
     *   // Drain via service_step
     *   transport_uart_service_step((runtime_component_t*)&uart_primary,
     *                                esp_timer_get_time());
     *   vTaskDelay(pdMS_TO_TICKS(100));
     *
     *   TEST_ASSERT_EQUAL(TRANSPORT_UART_TX_BUFFER_SIZE,
     *                     transport_uart_tx_free(&uart_primary));
     */
    TEST_IGNORE_MESSAGE("Requires physical hardware — implement for on-target build");
}

/* ================================================================
 * Test 4: No crashes during sustained operation
 * ================================================================ */

void test_sustained_no_crash(void)
{
    /* TODO: Run service_step in a tight loop and verify stability
     *
     * Steps:
     * 1. Call transport_uart_service_step() TEST_SUSTAINED_CYCLES times
     * 2. Verify no hard faults or watchdog resets
     * 3. Check stats are reasonable (rx_total should increase if UM980 active)
     * 4. Check rx_overflows stays at 0 or is low (buffer sized for 921600 baud)
     *
     * Example implementation:
     *   for (int i = 0; i < TEST_SUSTAINED_CYCLES; i++) {
     *       transport_uart_service_step((runtime_component_t*)&uart_primary,
     *                                    esp_timer_get_time());
     *       transport_uart_service_step((runtime_component_t*)&uart_secondary,
     *                                    esp_timer_get_time());
     *       vTaskDelay(pdMS_TO_TICKS(10));
     *   }
     *
     *   uint32_t rx_total, rx_overflows;
     *   transport_uart_get_rx_stats(&uart_primary, &rx_total, &rx_overflows);
     *   ESP_LOGI("SMOKE", "Primary: rx_total=%lu, rx_overflows=%lu",
     *            (unsigned long)rx_total, (unsigned long)rx_overflows);
     *
     *   // If UM980 is connected and sending, we should have received data
     *   if (rx_total > 0) {
     *       TEST_ASSERT_EQUAL_MESSAGE(0, rx_overflows,
     *           "RX buffer overflowed — consider increasing TRANSPORT_UART_RX_BUFFER_SIZE");
     *   }
     *
     *   TEST_PASS();
     */
    TEST_IGNORE_MESSAGE("Requires physical hardware — implement for on-target build");
}

/* ================================================================
 * Test 5: NMEA checksum validation on received data
 * ================================================================ */

void test_nmea_checksum_valid(void)
{
    /* TODO: Read a complete NMEA sentence from RX and validate checksum
     *
     * Steps:
     * 1. Read bytes until '\n' is found (end of NMEA sentence)
     * 2. Parse: $<payload>*<checksum>\r\n
     * 3. XOR all bytes between $ and *
     * 4. Compare with parsed checksum
     *
     * Example implementation:
     *   uint8_t sentence[256];
     *   size_t idx = 0;
     *   int found = 0;
     *
     *   for (int retries = 0; retries < 50; retries++) {
     *       transport_uart_service_step((runtime_component_t*)&uart_primary,
     *                                    esp_timer_get_time());
     *       vTaskDelay(pdMS_TO_TICKS(10));
     *
     *       size_t avail = transport_uart_rx_available(&uart_primary);
     *       if (avail > 0) {
     *           size_t to_read = avail > (sizeof(sentence) - 1 - idx)
     *                           ? (sizeof(sentence) - 1 - idx) : avail;
     *           idx += transport_uart_rx_read(&uart_primary, &sentence[idx], to_read);
     *
     *           // Look for newline
     *           for (size_t j = 0; j < idx; j++) {
     *               if (sentence[j] == '\n') { found = 1; break; }
     *           }
     *           if (found) break;
     *       }
     *   }
     *
     *   TEST_ASSERT_MESSAGE(found, "No complete NMEA sentence received");
     *
     *   // Validate checksum
     *   uint8_t cs = 0;
     *   size_t start = 1; // skip '$'
     *   size_t star_pos = 0;
     *   for (size_t j = start; j < idx; j++) {
     *       if (sentence[j] == '*') { star_pos = j; break; }
     *       cs ^= sentence[j];
     *   }
     *   TEST_ASSERT_GREATER_THAN(0, star_pos);
     *
     *   // Parse expected checksum (2 hex chars after '*')
     *   char cs_str[3] = {0};
     *   cs_str[0] = sentence[star_pos + 1];
     *   cs_str[1] = sentence[star_pos + 2];
     *   uint8_t expected = (uint8_t)strtol(cs_str, NULL, 16);
     *   TEST_ASSERT_EQUAL_MESSAGE(expected, cs, "NMEA checksum mismatch");
     *
     *   ESP_LOGI("SMOKE", "NMEA checksum validated: %s", (char*)sentence);
     */
    TEST_IGNORE_MESSAGE("Requires physical hardware — implement for on-target build");
}

/* ---- Runner ---- */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_uart_init_both_ports);
    RUN_TEST(test_rx_receives_nmea);
    RUN_TEST(test_tx_accepts_rtcm);
    RUN_TEST(test_sustained_no_crash);
    RUN_TEST(test_nmea_checksum_valid);

    return UNITY_END();
}
