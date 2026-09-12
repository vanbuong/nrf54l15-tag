/*
 * HOLyiot 25025 sensor backend.
 *
 *   SHT40    -> "sensirion,sht4x"   (I2C) temperature, humidity
 *   LPS22HB  -> "holyiot,lps22hb"   (SPI) pressure, local driver
 *   LIS2DH12 -> "st,lis2dh"         (SPI) accelerometer, polled only
 *   BY25Q16  -> "jedec,spi-nor"     (SPI) external flash for both logs
 *
 * INT1/INT2 land on port P2, which has no GPIOTE, so there is no wake
 * source — sensor_board_wake_source() returns NULL.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>

#include "sensor_board.h"

LOG_MODULE_REGISTER(sensor_holyiot, LOG_LEVEL_INF);

#define HAS_EXT_FLASH IS_ENABLED(CONFIG_SMART_TAG_FLASH_JEDEC_ID)

static const struct device *const dev_env = DEVICE_DT_GET(DT_ALIAS(sht40));
static const struct device *const dev_accel = DEVICE_DT_GET(DT_ALIAS(lis2dh12));
static const struct device *const dev_baro = DEVICE_DT_GET(DT_ALIAS(lps22));

#if HAS_EXT_FLASH
static const struct device *const dev_flash = DEVICE_DT_GET(DT_ALIAS(ext_flash));
#endif

int sensor_board_init(struct sensor_board_init *init)
{
	bool ok = true;

	if (sensor_board_device_ready(dev_env, SENSOR_VALID_TEMP_RH, init)) {
		/* SHT40 is T + RH from one fetch. */
	} else {
		ok = false;
	}

	if (sensor_board_device_ready(dev_baro, SENSOR_VALID_PRESSURE, init)) {
		/* ok */
	} else {
		ok = false;
	}

	if (!sensor_board_device_ready(dev_accel, SENSOR_VALID_ACCEL, init)) {
		ok = false;
	}

#if HAS_EXT_FLASH
	if (!sensor_board_device_ready(dev_flash, 0, init)) {
		ok = false;
	}
#endif

	return ok ? 0 : -ENODEV;
}

int sensor_board_read_environment(sensor_data_t *data)
{
	struct sensor_value value;

	if (device_is_ready(dev_env) && sensor_sample_fetch(dev_env) == 0) {
		if (sensor_channel_get(dev_env, SENSOR_CHAN_AMBIENT_TEMP,
				       &value) == 0) {
			data->temperature_c_x100 =
				(int16_t)(sensor_value_to_double(&value) * 100.0);
		}

		if (sensor_channel_get(dev_env, SENSOR_CHAN_HUMIDITY,
				       &value) == 0) {
			data->humidity_x100 =
				(uint16_t)(sensor_value_to_double(&value) * 100.0);
		}

		data->valid |= SENSOR_VALID_TEMP_RH;
	}

	if (device_is_ready(dev_baro) && sensor_sample_fetch(dev_baro) == 0) {
		if (sensor_channel_get(dev_baro, SENSOR_CHAN_PRESS, &value) == 0) {
			data->pressure_pa =
				(int32_t)(sensor_value_to_double(&value) * 1000.0);
			data->valid |= SENSOR_VALID_PRESSURE;
		}
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
	return 0;
}

const struct device *sensor_board_wake_source(void)
{
	return NULL;
}

int sensor_board_flash_jedec_id(uint8_t id[3])
{
#if HAS_EXT_FLASH
	if (!device_is_ready(dev_flash)) {
		return -ENODEV;
	}

	return flash_read_jedec_id(dev_flash, id);
#else
	ARG_UNUSED(id);
	return -ENOTSUP;
#endif
}
