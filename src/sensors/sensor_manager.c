#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <string.h>

#include "sensor_manager.h"

LOG_MODULE_REGISTER(sensor_manager, LOG_LEVEL_INF);

/*
 * Two boards, two entirely different sensor sets, one wrapper. Everything is
 * reached through a devicetree alias, so nothing below names a bus.
 *
 * nRF54L15 Tag (PCA20072):
 *   BME688   -> "bosch,bme680"      (I2C) temperature, humidity, pressure, gas
 *   ADXL367  -> "adi,adxl367"       (I2C) accelerometer, and the wake source
 *   BMI270   -> "bosch,bmi270"      (SPI) 6-axis IMU, read only while moving
 *   no external flash - U8 is not fitted, the logs live in internal RRAM
 *
 * HOLyiot 25025:
 *   SHT40    -> "sensirion,sht4x"   (I2C) temperature, humidity
 *   LPS22HB  -> "holyiot,lps22hb"   (SPI) pressure, via the local driver in
 *               drivers/sensor/lps22hb_spi: the fitted part is a real LPS22HB
 *               (WHO_AM_I 0xB1) and the in-tree "st,lps22hh" driver rejects
 *               any ID but 0xB3 - see README.md
 *   LIS2DH12 -> "st,lis2dh"         (SPI) accelerometer, polled only
 *   BY25Q16  -> "jedec,spi-nor"     (SPI) external flash for both logs
 *
 * The structural difference is that the BME688 answers temperature, humidity,
 * pressure AND gas from a single fetch, where the old board needed two
 * devices for the first three and had no gas sensor at all.
 */
#define HAS_BME688	DT_NODE_EXISTS(DT_ALIAS(bme688))
#define HAS_IMU		DT_NODE_EXISTS(DT_ALIAS(bmi270))
/*
 * CONFIG_SMART_TAG_FLASH_JEDEC_ID rather than the alias alone: it is set from
 * the devicetree in the application Kconfig and pulls in FLASH_JESD216_API,
 * which is what makes flash_read_jedec_id() link.
 */
#define HAS_EXT_FLASH	IS_ENABLED(CONFIG_SMART_TAG_FLASH_JEDEC_ID)

#if HAS_BME688
static const struct device *const dev_env = DEVICE_DT_GET(DT_ALIAS(bme688));
static const struct device *const dev_accel = DEVICE_DT_GET(DT_ALIAS(adxl367));
#else
static const struct device *const dev_env = DEVICE_DT_GET(DT_ALIAS(sht40));
static const struct device *const dev_accel = DEVICE_DT_GET(DT_ALIAS(lis2dh12));
static const struct device *const dev_baro = DEVICE_DT_GET(DT_ALIAS(lps22));
#endif

#if HAS_IMU
static const struct device *const dev_imu = DEVICE_DT_GET(DT_ALIAS(bmi270));
#endif

#if HAS_EXT_FLASH
static const struct device *const dev_flash = DEVICE_DT_GET(DT_ALIAS(ext_flash));
#endif

/*
 * Battery. The schematic wires a CR2032 through a P-channel FET straight to
 * VCC with no regulator, so the SoC supply rail is the cell voltage. The
 * nRF54L15 SAADC can measure VDD internally, which is why no external
 * divider is needed - and why none is fitted. (Not AVDD: that channel is the
 * internal 0.9 V analog supply rail, not the battery - see the board dts.)
 */
static const struct adc_dt_spec battery_adc =
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

/* Discharge curve for a CR2032 under a light pulsed load. */
#define BATTERY_FULL_MV	3000
#define BATTERY_EMPTY_MV 2000

/* 1 g in the milli-g units used throughout the application. */
#define ONE_G_MG 1000

/* Standard gravity, for converting the sensor API's m/s^2 to milli-g. */
#define MILLI_G_PER_MS2 (1000.0 / 9.80665)

/*
 * Deviation from 1 g that counts as "the tag is moving" for the purpose of
 * deciding whether to spend power on the IMU this cycle. Deliberately a
 * constant rather than config.motion_threshold_mg: this is a power decision
 * taken before motion classification runs, not the classification itself,
 * and it wants hysteresis-free simplicity. It mirrors the default
 * motion_threshold_mg so the two agree in the common case.
 */
#define IMU_ACTIVE_DEVIATION_MG 150

static uint8_t init_faults;

/* Bits this board's fitted sensors can actually produce - see sensor_manager_expected_valid(). */
static uint8_t expected_valid;

/*
 * Integer square root. Avoids pulling in libm for one magnitude calculation
 * that only needs milli-g resolution.
 */
static uint32_t isqrt32(uint32_t value)
{
	uint32_t rem = 0;
	uint32_t root = 0;

	for (int i = 0; i < 16; i++) {
		root <<= 1;
		rem = (rem << 2) | (value >> 30);
		value <<= 2;

		if (root < rem) {
			rem -= root | 1U;
			root += 2U;
		}
	}

	return root >> 1;
}

static bool check(const struct device *dev, uint8_t flag)
{
	if (!device_is_ready(dev)) {
		LOG_ERR("%s is not ready", dev->name);
		init_faults |= flag;
		return false;
	}

	LOG_INF("%s ready", dev->name);
	return true;
}

int sensor_manager_init(void)
{
	bool ok = true;

	init_faults = 0;
	expected_valid = 0;

	/*
	 * expected_valid is built from what actually came up, not from a
	 * hard-coded constant. app_state.c compares a sample's valid mask
	 * against it to decide whether to raise EVENT_SENSOR_FAULT; a fixed
	 * constant would fault permanently the moment the sensor set changed,
	 * which is exactly what this port does.
	 */
	if (check(dev_env, SENSOR_VALID_TEMP_RH)) {
		expected_valid |= SENSOR_VALID_TEMP_RH;
#if HAS_BME688
		/* One device, four quantities. */
		expected_valid |= SENSOR_VALID_PRESSURE | SENSOR_VALID_GAS;
#endif
	} else {
		ok = false;
	}

#if !HAS_BME688
	if (check(dev_baro, SENSOR_VALID_PRESSURE)) {
		expected_valid |= SENSOR_VALID_PRESSURE;
	} else {
		ok = false;
	}
#endif

	if (check(dev_accel, SENSOR_VALID_ACCEL)) {
		expected_valid |= SENSOR_VALID_ACCEL;
	} else {
		ok = false;
	}

	/*
	 * The IMU is not in expected_valid on purpose: it is read only while
	 * the tag is moving, so a stationary sample legitimately has
	 * SENSOR_VALID_GYRO clear and must not count as a fault.
	 */
#if HAS_IMU
	if (!check(dev_imu, 0)) {
		ok = false;
	}
#endif

#if HAS_EXT_FLASH
	ok &= check(dev_flash, 0);
#endif

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
#if HAS_BME688
	/* ADXL367: INT1 on P0.03, which has GPIOTE and pin sense. */
	return dev_accel;
#else
	/*
	 * The LIS2DH12's INT1/INT2 are on port P2, which has no GPIOTE and no
	 * SENSE, so this board has no wake source at all - motion is polled.
	 */
	return NULL;
#endif
}

int sensor_manager_read_environment(sensor_data_t *data)
{
	struct sensor_value value;
	uint16_t millivolts = 0;
	uint8_t percent = 0;
	int err;

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

#if HAS_BME688
		/*
		 * The same fetch already carries pressure and gas - the BME688
		 * measures all four in one conversion, so there is no second
		 * device to poll here the way the old board needed for the
		 * LPS22HB.
		 */
		if (sensor_channel_get(dev_env, SENSOR_CHAN_PRESS, &value) == 0) {
			/* The sensor API reports kPa; the log stores Pa. */
			data->pressure_pa =
				(int32_t)(sensor_value_to_double(&value) * 1000.0);
			data->valid |= SENSOR_VALID_PRESSURE;
		}

		/*
		 * Gas resistance is plain ohms in val1. It falls as VOC
		 * concentration rises, and needs tens of minutes of burn-in
		 * before the absolute value means anything - which is why the
		 * alarm threshold defaults to disabled.
		 */
		if (sensor_channel_get(dev_env, SENSOR_CHAN_GAS_RES, &value) == 0) {
			data->gas_resistance_ohm = (uint32_t)value.val1;
			data->valid |= SENSOR_VALID_GAS;
		}
#endif
	}

#if !HAS_BME688
	if (device_is_ready(dev_baro) && sensor_sample_fetch(dev_baro) == 0) {
		if (sensor_channel_get(dev_baro, SENSOR_CHAN_PRESS, &value) == 0) {
			/* The sensor API reports kPa; the log stores Pa. */
			data->pressure_pa =
				(int32_t)(sensor_value_to_double(&value) * 1000.0);
			data->valid |= SENSOR_VALID_PRESSURE;
		}
	}
#endif

	/*
	 * Via locals, not &data->battery_mv: sensor_data_t is __packed as of
	 * protocol v3, so taking the address of a member yields a possibly
	 * unaligned pointer. Harmless on this core, but it is a real trap on
	 * stricter targets and the compiler is right to warn.
	 */
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
	struct sensor_value axes[3];
	int32_t sum;

	if (!device_is_ready(dev_accel) || sensor_sample_fetch(dev_accel) != 0) {
		return -EIO;
	}

	if (sensor_channel_get(dev_accel, SENSOR_CHAN_ACCEL_XYZ, axes) != 0) {
		return -EIO;
	}

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

#if HAS_IMU
	/*
	 * The BMI270 is an order of magnitude hungrier than the ADXL367, and a
	 * gyroscope reading from a tag sitting on a shelf is noise around zero.
	 * So it is only read when this sample already shows movement: the
	 * ADXL367 stays on permanently as the cheap always-on sensor and the
	 * wake source, and the IMU is spun up only when there is something to
	 * measure. On a stationary sample SENSOR_VALID_GYRO stays clear, which
	 * is why the IMU is not part of expected_valid.
	 */
	if (abs((int32_t)data->magnitude_mg - ONE_G_MG) >= IMU_ACTIVE_DEVIATION_MG) {
		struct sensor_value gyro[3];

		if (device_is_ready(dev_imu) &&
		    sensor_sample_fetch(dev_imu) == 0 &&
		    sensor_channel_get(dev_imu, SENSOR_CHAN_GYRO_XYZ, gyro) == 0) {
			/* The sensor API reports rad/s; the wire uses 0.1 deg/s. */
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
#endif

	return 0;
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

	if (mv >= BATTERY_FULL_MV) {
		*percent = 100U;
	} else if (mv <= BATTERY_EMPTY_MV) {
		*percent = 0U;
	} else {
		*percent = (uint8_t)(((mv - BATTERY_EMPTY_MV) * 100) /
				     (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
	}

	return 0;
}

int sensor_manager_flash_jedec_id(uint8_t id[3])
{
#if HAS_EXT_FLASH
	if (!device_is_ready(dev_flash)) {
		return -ENODEV;
	}

	return flash_read_jedec_id(dev_flash, id);
#else
	/*
	 * No external flash on this assembly (PCA20072 BOM: U8 Not Fitted).
	 * The logs live in internal RRAM, which has no JEDEC identity to read.
	 */
	ARG_UNUSED(id);
	return -ENOTSUP;
#endif
}
