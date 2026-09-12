#include "unity.h"
#include "zigbee/zigbee_reporting.h"

static sensor_data_t env(int16_t t, uint16_t rh, int32_t pa, uint8_t batt,
			 uint32_t ts)
{
	sensor_data_t data = { 0 };

	data.timestamp = ts;
	data.temperature_c_x100 = t;
	data.humidity_x100 = rh;
	data.pressure_pa = pa;
	data.battery_percent = batt;
	data.valid = SENSOR_VALID_TEMP_RH | SENSOR_VALID_PRESSURE |
		     SENSOR_VALID_BATTERY;
	return data;
}

static tag_status_t quiet_status(void)
{
	tag_status_t status = { 0 };

	status.motion_state = MOTION_STATE_STATIONARY;
	return status;
}

static tag_config_t defaults(void)
{
	return (tag_config_t)TAG_CONFIG_DEFAULTS;
}

void test_reporting_first_sample_sends_all(void)
{
	sensor_data_t data = env(2500, 4000, 101325, 90, 10);
	tag_status_t status = quiet_status();
	tag_config_t config = defaults();

	zigbee_reporting_init();
	TEST_ASSERT_EQUAL_HEX32(REPORT_TEMPERATURE | REPORT_HUMIDITY |
					REPORT_PRESSURE | REPORT_BATTERY |
					REPORT_TAG_MONITOR,
				zigbee_reporting_evaluate(&data, &status,
							  &config));
}

void test_reporting_below_delta_is_quiet(void)
{
	sensor_data_t data = env(2500, 4000, 101325, 90, 10);
	tag_status_t status = quiet_status();
	tag_config_t config = defaults();

	zigbee_reporting_init();
	zigbee_reporting_commit(&data, &status,
				zigbee_reporting_evaluate(&data, &status,
							  &config));

	data.timestamp = 20;
	data.temperature_c_x100 = 2540; /* +0.40 C, delta is 0.50 */
	TEST_ASSERT_EQUAL_HEX32(0, zigbee_reporting_evaluate(&data, &status,
							     &config));
}

void test_reporting_humidity_pressure_battery_deltas(void)
{
	sensor_data_t data = env(2500, 4000, 101325, 90, 10);
	tag_status_t status = quiet_status();
	tag_config_t config = defaults();

	zigbee_reporting_init();
	zigbee_reporting_commit(&data, &status,
				REPORT_TEMPERATURE | REPORT_HUMIDITY |
					REPORT_PRESSURE | REPORT_BATTERY |
					REPORT_TAG_MONITOR);

	data.timestamp = 11;
	data.humidity_x100 = 4400; /* +4 %RH, delta is 3 */
	TEST_ASSERT_EQUAL_HEX32(REPORT_HUMIDITY,
				zigbee_reporting_evaluate(&data, &status,
							  &config));

	data.humidity_x100 = 4000;
	data.pressure_pa = 101325 + 200;
	TEST_ASSERT_EQUAL_HEX32(REPORT_PRESSURE,
				zigbee_reporting_evaluate(&data, &status,
							  &config));

	data.pressure_pa = 101325;
	data.battery_percent = 88;
	TEST_ASSERT_EQUAL_HEX32(REPORT_BATTERY,
				zigbee_reporting_evaluate(&data, &status,
							  &config));
}

void test_reporting_temperature_delta(void)
{
	sensor_data_t data = env(2500, 4000, 101325, 90, 10);
	tag_status_t status = quiet_status();
	tag_config_t config = defaults();

	zigbee_reporting_init();
	zigbee_reporting_commit(&data, &status,
				REPORT_TEMPERATURE | REPORT_HUMIDITY |
					REPORT_PRESSURE | REPORT_BATTERY |
					REPORT_TAG_MONITOR);

	data.timestamp = 20;
	data.temperature_c_x100 = 2550;
	TEST_ASSERT_EQUAL_HEX32(REPORT_TEMPERATURE,
				zigbee_reporting_evaluate(&data, &status,
							  &config));
}

void test_reporting_heartbeat_on_max_interval(void)
{
	sensor_data_t data = env(2500, 4000, 101325, 90, 10);
	tag_status_t status = quiet_status();
	tag_config_t config = defaults();

	zigbee_reporting_init();
	zigbee_reporting_commit(&data, &status,
				REPORT_TEMPERATURE | REPORT_HUMIDITY |
					REPORT_PRESSURE | REPORT_BATTERY |
					REPORT_TAG_MONITOR);

	data.timestamp = 10 + config.report_max_interval_s;
	TEST_ASSERT_EQUAL_HEX32(REPORT_TEMPERATURE | REPORT_HUMIDITY |
					REPORT_PRESSURE | REPORT_BATTERY |
					REPORT_TAG_MONITOR,
				zigbee_reporting_evaluate(&data, &status,
							  &config));
}

void test_reporting_motion_is_immediate(void)
{
	sensor_data_t data = env(2500, 4000, 101325, 90, 10);
	tag_status_t status = quiet_status();
	tag_config_t config = defaults();

	zigbee_reporting_init();
	zigbee_reporting_commit(&data, &status,
				REPORT_TEMPERATURE | REPORT_HUMIDITY |
					REPORT_PRESSURE | REPORT_BATTERY |
					REPORT_TAG_MONITOR);

	status.motion_state = MOTION_STATE_MOVING;
	data.timestamp = 11;
	TEST_ASSERT_EQUAL_HEX32(REPORT_TAG_MONITOR,
				zigbee_reporting_evaluate(&data, &status,
							  &config));
}

void test_reporting_gas_delta(void)
{
	sensor_data_t data = env(2500, 4000, 101325, 90, 10);
	tag_status_t status = quiet_status();
	tag_config_t config = defaults();

	data.gas_resistance_ohm = 50000;
	data.valid |= SENSOR_VALID_GAS;

	zigbee_reporting_init();
	zigbee_reporting_commit(&data, &status,
				REPORT_TEMPERATURE | REPORT_HUMIDITY |
					REPORT_PRESSURE | REPORT_BATTERY |
					REPORT_TAG_MONITOR);

	data.timestamp = 11;
	data.gas_resistance_ohm = 40000; /* 10 kOhm drop, delta is 5 kOhm */
	TEST_ASSERT_EQUAL_HEX32(REPORT_TAG_MONITOR,
				zigbee_reporting_evaluate(&data, &status,
							  &config));
}

void test_reporting_gas_disabled_when_delta_zero(void)
{
	sensor_data_t data = env(2500, 4000, 101325, 90, 10);
	tag_status_t status = quiet_status();
	tag_config_t config = defaults();

	config.report_gas_delta_ohm = 0;
	data.gas_resistance_ohm = 50000;
	data.valid |= SENSOR_VALID_GAS;

	zigbee_reporting_init();
	zigbee_reporting_commit(&data, &status,
				REPORT_TEMPERATURE | REPORT_HUMIDITY |
					REPORT_PRESSURE | REPORT_BATTERY |
					REPORT_TAG_MONITOR);

	data.timestamp = 11;
	data.gas_resistance_ohm = 1;
	TEST_ASSERT_EQUAL_HEX32(0, zigbee_reporting_evaluate(&data, &status,
							     &config));
}

void test_reporting_force_resets_baseline(void)
{
	sensor_data_t data = env(2500, 4000, 101325, 90, 10);
	tag_status_t status = quiet_status();
	tag_config_t config = defaults();

	zigbee_reporting_init();
	zigbee_reporting_commit(&data, &status,
				REPORT_TEMPERATURE | REPORT_HUMIDITY |
					REPORT_PRESSURE | REPORT_BATTERY |
					REPORT_TAG_MONITOR);
	zigbee_reporting_force();
	TEST_ASSERT_NOT_EQUAL(0, zigbee_reporting_evaluate(&data, &status,
							   &config));
}

void test_reporting_commit_zero_is_noop(void)
{
	sensor_data_t data = env(2500, 4000, 101325, 90, 10);
	tag_status_t status = quiet_status();
	tag_config_t config = defaults();

	zigbee_reporting_init();
	zigbee_reporting_commit(&data, &status, 0);
	TEST_ASSERT_NOT_EQUAL(0, zigbee_reporting_evaluate(&data, &status,
							   &config));
}
