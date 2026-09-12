/*
 * Sensor manager - PLAN.md section 4.
 *
 * The single place that talks to hardware. Presents one sensor_data_t to the
 * application, which never sees a driver handle.
 *
 * The per-chip files PLAN.md sketches are deliberately absent: every sensor
 * is bound from the device tree to a Zephyr sensor-API driver, so a
 * hand-written register layer in the application would be duplicated code
 * with no owner. This module is the thin wrapper the plan asks for; the
 * driver bindings live in the board device tree.
 *
 * Board differences live in sensor_holyiot.c / sensor_nrf54l15tag.c, which
 * implement the sensor_board_* API. CMake compiles exactly one of them.
 * Nothing above this module knows which parts are fitted:
 *
 *   nRF54L15 Tag   BME688 (T/RH/pressure/gas), ADXL367, BMI270
 *   HOLyiot 25025  SHT40 (T/RH), LPS22HB (pressure), LIS2DH12
 *
 * The shape difference that matters upward is that the BME688 answers four
 * quantities from one fetch, so on the Tag a single device carries three of
 * the SENSOR_VALID_* bits.
 */
#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <zephyr/device.h>

#include "app/smart_tag.h"

/** Binds the sensor, ADC and flash devices. Returns 0 when all are ready. */
int sensor_manager_init(void);

/**
 * Reads one full measurement set. Fields whose sensor failed are left zero
 * and their SENSOR_VALID_* bit is clear, so a single dead sensor does not
 * lose the rest of the sample. Always returns a usable struct.
 */
int sensor_manager_read(sensor_data_t *data);

/**
 * Reads only the environmental channels - the normal low-power cycle in
 * PLAN.md section 4 (wake, measure, store, sleep).
 */
int sensor_manager_read_environment(sensor_data_t *data);

/**
 * Reads the accelerometer, for the motion-driven path. On a board with an
 * IMU the gyroscope is read too, but only when the accelerometer already
 * shows movement - see the comment in sensor_manager.c. A stationary sample
 * therefore has SENSOR_VALID_GYRO clear by design, not by failure.
 */
int sensor_manager_read_motion(sensor_data_t *data);

/** Battery, measured on the internal VDD channel (no external divider). */
int sensor_manager_read_battery(uint16_t *millivolts, uint8_t *percent);

/**
 * Reads the external NOR JEDEC ID, used at bring-up to confirm the part.
 * Returns -ENOTSUP on a board with no external flash fitted.
 */
int sensor_manager_flash_jedec_id(uint8_t id[3]);

/** Bit mask of SENSOR_VALID_* for devices that failed to bind at init. */
uint8_t sensor_manager_faults(void);

/**
 * Bit mask of SENSOR_VALID_* that this board's fitted, working sensors can
 * actually produce, determined at init. A sample missing any of these bits
 * is a fault; a sample missing anything else is not. Callers must compare
 * against this rather than a hard-coded set, because the two boards produce
 * different masks - and the IMU is deliberately excluded, since it is only
 * read while the tag is moving.
 */
uint8_t sensor_manager_expected_valid(void);

/**
 * The device whose interrupt can wake the sampling loop early, or NULL if
 * this board has none and motion has to be polled. On the nRF54L15 Tag this
 * is the ADXL367, whose INT1 is on P0.03 (GPIOTE + pin sense).
 */
const struct device *sensor_manager_wake_source(void);

#endif /* SENSOR_MANAGER_H */
