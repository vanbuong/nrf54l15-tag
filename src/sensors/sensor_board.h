/*
 * Board sensor backend.
 *
 * sensor_manager.c is the public API and the common pieces (battery,
 * magnitude, orchestration). Each supported board implements this
 * interface in one file:
 *
 *   sensor_holyiot.c       SHT40 + LPS22HB + LIS2DH12 + optional NOR
 *   sensor_nrf54l15tag.c   BME688 + ADXL367 + BMI270
 *
 * CMake picks exactly one from the device tree (bme688 node present or
 * not), the same way the validation image picks its board tests.
 */
#ifndef SENSOR_BOARD_H
#define SENSOR_BOARD_H

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include "app/smart_tag.h"

struct sensor_board_init {
	uint8_t *expected_valid;
	uint8_t *init_faults;
};

/**
 * Logs whether @p dev came up and, if @p valid_bit is non-zero, records
 * it as an expected capability. Returns true when the device is ready.
 */
bool sensor_board_device_ready(const struct device *dev, uint8_t valid_bit,
			       struct sensor_board_init *init);

/** Converts SENSOR_CHAN_ACCEL_XYZ (m/s^2) into milli-g plus magnitude. */
void sensor_board_apply_accel(sensor_data_t *data,
			      const struct sensor_value axes[3]);

int sensor_board_init(struct sensor_board_init *init);

int sensor_board_read_environment(sensor_data_t *data);

int sensor_board_read_motion(sensor_data_t *data);

const struct device *sensor_board_wake_source(void);

/** JEDEC ID of the external NOR, or -ENOTSUP when none is fitted. */
int sensor_board_flash_jedec_id(uint8_t id[3]);

#endif /* SENSOR_BOARD_H */
