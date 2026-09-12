/*
 * Nordic nRF54L15 Tag (PCA20072) sensor backend.
 *
 *   BME688   -> "bosch,bme680"  (I2C) temperature, humidity, pressure, gas
 *   ADXL367  -> "adi,adxl367"   (I2C) accelerometer and the wake source
 *   BMI270   -> "bosch,bmi270"  (SPI) 6-axis IMU, read only while moving
 *
 * No external flash: BOM U8 is not fitted, logs live in internal RRAM.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>

#include "sensor_board.h"

LOG_MODULE_REGISTER(sensor_nrf54l15tag, LOG_LEVEL_INF);

/*
 * Deviation from 1 g that counts as "the tag is moving" for the purpose of
 * deciding whether to spend power on the IMU this cycle. Deliberately a
 * constant rather than config.motion_threshold_mg: this is a power decision
 * taken before motion classification runs.
 */
#define IMU_ACTIVE_DEVIATION_MG 150

static const struct device *const dev_env = DEVICE_DT_GET(DT_ALIAS(bme688));
static const struct device *const dev_accel = DEVICE_DT_GET(DT_ALIAS(adxl367));
static const struct device *const dev_imu = DEVICE_DT_GET(DT_ALIAS(bmi270));

int sensor_board_init(struct sensor_board_init *init)
{
	bool ok = true;

	if (sensor_board_device_ready(dev_env, SENSOR_VALID_TEMP_RH, init)) {
		/* One device, four quantities. */
		*init->expected_valid |= SENSOR_VALID_PRESSURE | SENSOR_VALID_GAS;
	} else {
		ok = false;
	}

	if (!sensor_board_device_ready(dev_accel, SENSOR_VALID_ACCEL, init)) {
		ok = false;
	}

	/*
	 * The IMU is not in expected_valid: it is read only while moving, so
	 * a stationary sample legitimately has SENSOR_VALID_GYRO clear.
	 */
	if (!sensor_board_device_ready(dev_imu, 0, init)) {
		ok = false;
	}

	return ok ? 0 : -ENODEV;
}

int sensor_board_read_environment(sensor_data_t *data)
{
	struct sensor_value value;

	if (!device_is_ready(dev_env) || sensor_sample_fetch(dev_env) != 0) {
		return 0;
	}

	if (sensor_channel_get(dev_env, SENSOR_CHAN_AMBIENT_TEMP, &value) == 0) {
		data->temperature_c_x100 =
			(int16_t)(sensor_value_to_double(&value) * 100.0);
	}

	if (sensor_channel_get(dev_env, SENSOR_CHAN_HUMIDITY, &value) == 0) {
		data->humidity_x100 =
			(uint16_t)(sensor_value_to_double(&value) * 100.0);
	}

	data->valid |= SENSOR_VALID_TEMP_RH;

	if (sensor_channel_get(dev_env, SENSOR_CHAN_PRESS, &value) == 0) {
		data->pressure_pa =
			(int32_t)(sensor_value_to_double(&value) * 1000.0);
		data->valid |= SENSOR_VALID_PRESSURE;
	}

	if (sensor_channel_get(dev_env, SENSOR_CHAN_GAS_RES, &value) == 0) {
		data->gas_resistance_ohm = (uint32_t)value.val1;
		data->valid |= SENSOR_VALID_GAS;
	}

	return 0;
}

int sensor_board_read_motion(sensor_data_t *data)
{
	struct sensor_value axes[3];

	if (!device_is_ready(dev_accel) || sensor_sample_fetch(dev_accel) != 0) {
		return -EIO;
	}

	if (sensor_channel_get(dev_accel, SENSOR_CHAN_ACCEL_XYZ, axes) != 0) {
		return -EIO;
	}

	sensor_board_apply_accel(data, axes);

	if (abs((int32_t)data->magnitude_mg - SMART_TAG_ONE_G_MG) <
	    IMU_ACTIVE_DEVIATION_MG) {
		return 0;
	}

	if (device_is_ready(dev_imu) && sensor_sample_fetch(dev_imu) == 0) {
		struct sensor_value gyro[3];

		if (sensor_channel_get(dev_imu, SENSOR_CHAN_GYRO_XYZ, gyro) == 0) {
			data->gyro_x_dps_x10 =
				(int16_t)(sensor_value_to_double(&gyro[0]) *
					  (1800.0 / 3.14159265358979));
			data->gyro_y_dps_x10 =
				(int16_t)(sensor_value_to_double(&gyro[1]) *
					  (1800.0 / 3.14159265358979));
			data->gyro_z_dps_x10 =
				(int16_t)(sensor_value_to_double(&gyro[2]) *
					  (1800.0 / 3.14159265358979));
			data->valid |= SENSOR_VALID_GYRO;
		}
	}

	return 0;
}

const struct device *sensor_board_wake_source(void)
{
	return dev_accel;
}

int sensor_board_flash_jedec_id(uint8_t id[3])
{
	ARG_UNUSED(id);
	return -ENOTSUP;
}
