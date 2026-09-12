cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(smart_tag)

# Selected on the device tree, not on a Kconfig symbol. The validation
# image deliberately leaves every sensor driver off, so CONFIG_BME680 is
# always n there; the node exists either way.
dt_nodelabel(has_bme688 NODELABEL "bme688")

if(CONFIG_SMART_TAG_HW_VALIDATION)
  # Phase 1. A standalone peripheral test runner that shares only the
  # board definition with the application - no application sources at all.
  target_sources(app PRIVATE
      src/validation/hw_validation.c
  )

  if(DEFINED has_bme688)
    target_sources(app PRIVATE src/validation/hw_validation_tag.c)
  else()
    target_sources(app PRIVATE src/validation/hw_validation_holyiot.c)
  endif()
else()
  # Module layout follows DESIGN.md section 5.
  target_sources(app PRIVATE
      src/app/app_main.c
      src/app/app_state.c
      src/app/app_config.c

      src/sensors/sensor_manager.c

      src/motion/motion_manager.c
      src/motion/motion_classifier.c
      src/motion/shock_detector.c

      src/storage/flash_manager.c
      src/storage/event_log.c
      src/storage/sensor_log.c
      src/storage/config_storage.c
      src/storage/config_migrate.c

      src/ui/led_manager.c

      src/power/power_manager.c
      src/power/watchdog.c

      src/ble/ble_manager.c
      src/ble/ble_gatt.c
      src/ble/ble_config.c
      src/ble/ble_log.c

      src/zigbee/zigbee_manager.c
      src/zigbee/zigbee_clusters.c
      src/zigbee/zigbee_reporting.c
      src/zigbee/zigbee_ota.c
  )

  if(DEFINED has_bme688)
    target_sources(app PRIVATE src/sensors/sensor_nrf54l15tag.c)
  else()
    target_sources(app PRIVATE src/sensors/sensor_holyiot.c)
  endif()

  # Local out-of-tree driver for the LPS22HB (see the LPS22HB/LPS22HH note
  # in README.md). Compiled into "app" rather than zephyr_library() because
  # this CMakeLists.txt runs after find_package(Zephyr) has already read
  # ZEPHYR_LIBS — see the previous revision of this comment for the full
  # ordering argument.
  target_sources_ifdef(CONFIG_LPS22HB_SPI app PRIVATE
      drivers/sensor/lps22hb_spi/lps22hb_spi.c
  )
endif()

target_include_directories(app PRIVATE src)
