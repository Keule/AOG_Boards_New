# Install script for directory: C:/Users/uwe/.platformio/packages/framework-espidf

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/AOG_ESP_Multiboard")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "C:/Users/uwe/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump.exe")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/xtensa/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_gpio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_timer/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_pm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/mbedtls/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/bootloader/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esptool_py/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/partition_table/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_app_format/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_bootloader_format/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/app_update/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_partition/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/efuse/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/bootloader_support/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_mm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/spi_flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_system/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_common/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_rom/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/hal/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/log/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/heap/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/soc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_security/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_hw_support/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/freertos/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/newlib/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/pthread/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/cxx/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/__pio_env/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_gptimer/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_ringbuf/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_psram/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_uart/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/app_trace/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_event/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/nvs_flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_phy/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_usb_serial_jtag/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_vfs_console/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/vfs/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/lwip/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_netif_stack/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_netif/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/wpa_supplicant/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_coex/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_wifi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_spi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_gdbstub/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/bt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/unity/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/cmock/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/console/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_pcnt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_mcpwm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_ana_cmpr/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_i2s/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/sdmmc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_sdmmc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_sdspi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_sdio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_dac/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_bitscrambler/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_rmt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_tsens/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_sdm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_i2c/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_ledc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_parlio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_twai/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/driver/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/http_parser/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp-tls/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_adc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_isp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_cam/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_jpeg/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_ppa/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_driver_touch_sens/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_eth/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_hal_ieee802154/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_hid/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/tcp_transport/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_http_client/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_http_server/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_https_ota/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_https_server/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_lcd/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/protobuf-c/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/protocomm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/esp_local_ctrl/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/espcoredump/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/wear_levelling/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/fatfs/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/idf_test/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/ieee802154/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/json/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/mqtt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/nvs_sec_provider/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/openthread/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/perfmon/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/rt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/spiffs/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/touch_element/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/ulp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/usb/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/wifi_provisioning/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/feature_flags/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/board_profiles/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/runtime_types/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/runtime_components/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/runtime_stats/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/runtime_watchdog/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/runtime_fast/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/runtime/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/hal_backend/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/hal_uart/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/runtime_buffers/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/transport_uart/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/transport_udp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/transport_tcp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/protocol_nmea/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/runtime_snapshot/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/gnss_um980/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/gnss_dual_heading/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/ntrip_client/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/protocol_rtcm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/rtcm_router/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/protocol_aog/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/aog_navigation_app/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/hal_spi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/imu_bno085/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/ads1118/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/was_sensor/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/hal_gpio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/safety_failsafe/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/steering_control/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/actuator_drv8263h/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/aog_steering_app/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/app_core/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/main/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/cli/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/hal_eth/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/hal_nvs/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/hal_ota/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/hal_reset/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/hal_time/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/runtime_health/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/runtime_queue/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/terminal/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/uwe/Documents/Entwicklung/AgopenGPS/AOG_Boards_New/.pio/build/steer_esp32s3_t_eth_lite/esp-idf/transport_eth/cmake_install.cmake")
endif()

