cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(smart_tag)

if(CONFIG_SMART_TAG_HW_VALIDATION)
  # PLAN.md phase 1. A standalone peripheral test runner that shares only the
  # board definition with the application - no application sources at all, so
  # a failure here cannot be blamed on the firmware above the drivers.
  target_sources(app PRIVATE
      src/validation/hw_validation.c
  )

  # The bus and sensor tests are board-specific - the two boards share no
  # sensors at all - so exactly one of these provides
  # hw_validation_board_tests().
  #
  # Selected on the devicetree, not on a Kconfig symbol. CONFIG_BME680 would
  # be the obvious test but is always n here: this build deliberately leaves
  # every sensor driver out and talks to the parts through raw register
  # access (see hw_validation.h). The node exists either way.
  dt_nodelabel(has_bme688 NODELABEL "bme688")
  if(DEFINED has_bme688)
    target_sources(app PRIVATE src/validation/hw_validation_tag.c)
  else()
    target_sources(app PRIVATE src/validation/hw_validation_holyiot.c)
  endif()
else()
  # Module layout follows PLAN.md section 3.
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

      src/ui/led_manager.c

      src/power/power_manager.c

      src/ble/ble_manager.c
      src/ble/ble_gatt.c
      src/ble/ble_config.c
      src/ble/ble_log.c

      src/zigbee/zigbee_manager.c
      src/zigbee/zigbee_clusters.c
      src/zigbee/zigbee_reporting.c
      src/zigbee/zigbee_ota.c
  )

  # Local out-of-tree driver for the LPS22HB (see the LPS22HB/LPS22HH note
  # in README.md).
  #
  # Deliberately NOT zephyr_library(). That macro is only valid in "kernel
  # mode" - a CMakeLists.txt that Zephyr itself walks as part of its own
  # tree or a Zephyr module. It works by appending the new target to the
  # global ZEPHYR_LIBS property, and zephyr/CMakeLists.txt reads that
  # property once, at kernel.cmake:228, to build the link line and to hang
  # every library off zephyr_generated_headers (offsets.h, syscalls).
  #
  # That read happens inside find_package(Zephyr) above - i.e. before this
  # line ever executes. Appending afterwards is too late on both counts: the
  # archive would be compiled but left out of the link, and it would have no
  # ordering dependency on the generated headers. zephyr_append_cmake_library()
  # detects this (the zephyr_pre0 target already exists) and warns, so it is
  # not silent, but the warning is easy to miss in a full build log.
  #
  # Compiling the driver straight into "app", same as every source above, is
  # the supported way to do this short of making the app a real Zephyr
  # module. "app" is itself a Zephyr library, registered in kernel mode at
  # the right time, so the driver inherits its include paths, its compile
  # flags and its generated-header dependency.
  target_sources_ifdef(CONFIG_LPS22HB_SPI app PRIVATE
      drivers/sensor/lps22hb_spi/lps22hb_spi.c
  )
endif()

target_include_directories(app PRIVATE src)
