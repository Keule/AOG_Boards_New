# NAV-ETH-BRINGUP-001 — Browser Chatlog

## Task Prompt
NAV-ETH-BRINGUP-001: RTL8201/RMII Ethernet auf LilyGO T-ETH-Lite ESP32 real hochbringen

## WP-A: STUB Analysis
- STUB output comes from hw_runtime_diag.c lines 211-217 (hardcoded literals)
- hal_eth.c had 4 no-op stubs (init/deinit/get_mac/is_connected)
- transport_udp.c had ring buffers but no socket() — data silently discarded
- hal_eth_init() was never called in app_core.c
- ESP-IDF ETH driver IS enabled in sdkconfig (CONFIG_ETH_USE_ESP32_EMAC=y)
- lwip IS available (CONFIG_LWIP_MAX_SOCKETS=10)

## WP-B: Real ESP-IDF 6.x RMII Ethernet HAL
- Replaced all stubs in hal_eth.c with esp_eth_mac_new_esp32() + esp_eth_phy_new_generic()
- ESP-IDF 6.x API: parameter order swapped (esp32_config, mac_config)
- PHY config no longer has mdc_gpio_num/mdio_gpio_num — uses EMAC smi_gpio
- Event names: ETHERNET_EVENT_START (not STARTED), ETHERNET_EVENT_STOP (not STOPED)
- ip4addr_ntoa() now requires 3 args: (addr, buf, buflen) — ESP-IDF 6.x breaking change
- driver/gpio.h replaced by esp_driver_gpio component in ESP-IDF 6.x

## WP-C: PHY Power GPIO12
- gpio_set_direction(GPIO_NUM_12, GPIO_MODE_OUTPUT) + gpio_set_level(1)
- 50ms delay after power-on before PHY init

## WP-D: Link/IP Diagnostics
- hal_eth_status_t: initialized, link_up, got_ip, ip_addr[4], netmask[4], gw[4]
- Events logged: START, STOP, CONNECTED, DISCONNECTED, GOT_IP

## WP-E: Real UDP Transport
- Non-blocking lwip sockets with O_NONBLOCK
- Lazy socket open: waits for ETH IP before bind()
- Broadcast support for AgIO (INADDR_BROADCAST + SO_BROADCAST)
- sendto/recvfrom in service_step (NOT in fast path)
- Status counters: tx_packets, rx_packets, tx_errors, rx_errors

## WP-F: NTRIP Readiness
- hw_runtime_diag shows: NTRIP: eth_ready=0/1 config_ready=0 state=disabled

## Build Errors Encountered & Fixed
1. driver/gpio.h → esp_driver_gpio component (ESP-IDF 6.x)
2. ETHERNET_EVENT_STARTED → ETHERNET_EVENT_START
3. ETHERNET_EVENT_STOPPED → ETHERNET_EVENT_STOP
4. ip4addr_ntoa(addr) → esp_ip4addr_ntoa(addr, buf, buflen)
5. ip4_addr1..4 → esp_ip4_addr1..4
6. HAL_ERR_UNKNOWN → HAL_ERR_IO (not in hal_backend.h enum)
7. emb_esp_eth_rmii_clock_mode_t → emac_rmii_clock_mode_t
8. esp_eth_new_mac() → esp_eth_mac_new_esp32()
9. PHY mdc_gpio_num/mdio_gpio_num → esp32_config.smi_gpio.mdc_num/.mdio_num
10. esp_eth_new_phy() → esp_eth_phy_new_generic()
11. esp_netif_attach(netif, handle) → esp_eth_new_netif_glue() + esp_netif_attach(netif, glue)

## Build Result
pio run -e nav_esp32_t_eth_lite -> SUCCESS (32.42s)
Firmware: text=278225 data=92780 bss=19017 total=390022 (381KB)

## Hard Rules Verification
- No GNSS logic changed: YES
- No GNSS validity rules changed: YES
- No PGN214 semantics changed: YES
- No AOG frame format changes: YES
- No NTRIP logic changed: YES
- No steering changes: YES
- No blocking code in fast path: YES
