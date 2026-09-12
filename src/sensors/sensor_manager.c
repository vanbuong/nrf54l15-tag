/*
 * Sensor manager - public API and board-independent pieces.
 *
 * Board-specific buses and parts live in sensor_holyiot.c /
 * sensor_nrf54l15tag.c. Nothing above this module names a driver.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <string.h>

#include "sensor_manager.h"
#include "sensor_board.h"

LOG_MODULE_REGISTER(sensor_manager, LOG_LEVEL_INF);

static const struct adc_dt_spec battery_adc =
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

/* Standard gravity, for converting the sensor API's m/s^2 to milli-g. */
#define MILLI_G_PER_MS2 (1000.0 / 9.80665)

static uint8_t init_faults;
static uint8_t expected_valid;

static uint32_t isqrt32(uint32_t value)
{
	uint32_t rest = 0;
	uint32_t root = 0;

	for (int i = 0; i < 16; i++) {
		root <<= 1;
		rest = (rest << 2) | (value >> 30);
		value <<= 2;

		if (root < rest) {
			rest -= root | 1U;
			root += 2U;
		}
	}

	return root >> 1;
}

bool sensor_board_device_ready(const struct device *dev, uint8_t valid_bit,
			       struct sensor_board_init *init)
{
	if (!device_is_ready(dev)) {
		LOG_ERR("%s is not ready", dev->name);
		*init->init_faults |= valid_bit;
		return false;
	}

	LOG_INF("%s ready", dev->name);

	if (valid_bit != 0U) {
		*init->expected_valid |= valid_bit;
	}

	return true;
}

void sensor_board_apply_accel(sensor_data_t *data,
			      const struct sensor_value axes[3])
{
	int32_t sum;

	data->accel_x_mg = (int16_t)(sensor_value_to_double(&axes[0]) *
				     MILLI_G_PER_MS2);
	data->accel_y_mg = (int16_t)(sensor_value_to_double(&axes[1]) *
				     MILLI_G_PER_MS2);
	data->accel_z_mg = (int16_t)(sensor_value_to_double(&axes[2]) *
				     MILLI_G_PER_MS2);

	sum = (int32_t)data->accel_x_mg * data->accel_x_mg +
	      (int32_t)data->accel_y_mg * data->accel_y_mg +
	      (int32_t)data->accel_z_mg * data->accel_z_mg;

	data->magnitude_mg = (uint16_t)MIN(isqrt32((uint32_t)sum), UINT16_MAX);
	data->valid |= SENSOR_VALID_ACCEL;
}

int sensor_manager_init(void)
{
	struct sensor_board_init init = {
		.expected_valid = &expected_valid,
		.init_faults = &init_faults,
	};
	bool ok = true;

	init_faults = 0;
	expected_valid = 0;

	if (sensor_board_init(&init) != 0) {
		ok = false;
	}

	if (!adc_is_ready_dt(&battery_adc)) {
		LOG_ERR("battery ADC not ready");
		init_faults |= SENSOR_VALID_BATTERY;
		ok = false;
	} else if (adc_channel_setup_dt(&battery_adc) != 0) {
		LOG_ERR("battery ADC channel setup failed");
		init_faults |= SENSOR_VALID_BATTERY;
		ok = false;
	} else {
		expected_valid |= SENSOR_VALID_BATTERY;
	}

	return ok ? 0 : -ENODEV;
}

uint8_t sensor_manager_faults(void)
{
	return init_faults;
}

uint8_t sensor_manager_expected_valid(void)
{
	return expected_valid;
}

const struct device *sensor_manager_wake_source(void)
{
	return sensor_board_wake_source();
}

int sensor_manager_read_environment(sensor_data_t *data)
{
	uint16_t millivolts = 0;
	uint8_t percent = 0;
	int err;

	(void)sensor_board_read_environment(data);

	err = sensor_manager_read_battery(&millivolts, &percent);
	if (err == 0) {
		data->battery_mv = millivolts;
		data->battery_percent = percent;
		data->valid |= SENSOR_VALID_BATTERY;
	}

	return 0;
}

int sensor_manager_read_motion(sensor_data_t *data)
{
	return sensor_board_read_motion(data);
}

int sensor_manager_read(sensor_data_t *data)
{
	memset(data, 0, sizeof(*data));
	data->timestamp = (uint32_t)(k_uptime_get() / 1000);

	(void)sensor_manager_read_environment(data);
	(void)sensor_manager_read_motion(data);

	return 0;
}

int sensor_manager_read_battery(uint16_t *millivolts, uint8_t *percent)
{
	int16_t raw = 0;
	int32_t mv;
	int err;

	struct adc_sequence sequence = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};

	if (!adc_is_ready_dt(&battery_adc)) {
		return -ENODEV;
	}

	err = adc_sequence_init_dt(&battery_adc, &sequence);
	if (err) {
		return err;
	}

	err = adc_read_dt(&battery_adc, &sequence);
	if (err) {
		LOG_WRN("battery ADC read failed: %d", err);
		return err;
	}

	mv = raw;

	err = adc_raw_to_millivolts_dt(&battery_adc, &mv);
	if (err) {
		return err;
	}

	if (mv < 0) {
		mv = 0;
	}

	*millivolts = (uint16_t)mv;
	*percent = smart_tag_battery_percent(mv);

	return 0;
}

int sensor_manager_flash_jedec_id(uint8_t id[3])
{
	return sensor_board_flash_jedec_id(id);
}
