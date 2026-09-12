#include "unity.h"
#include "app/app_alarms.h"

static tag_config_t cfg(void)
{
	return (tag_config_t)TAG_CONFIG_DEFAULTS;
}

void test_alarms_temp_high_latches_once(void)
{
	tag_config_t config = cfg();
	sensor_data_t data = { 0 };
	struct app_alarm_event events[APP_ALARM_MAX_EVENTS];
	uint8_t n = 0;
	uint16_t flags = 0;

	data.valid = SENSOR_VALID_TEMP_RH;
	data.temperature_c_x100 = 4100; /* default high is 40.00 C */

	app_alarms_eval(&data, &config, 0, &flags, events, &n);
	TEST_ASSERT_BITS_HIGH(ALARM_TEMP_HIGH, flags);
	TEST_ASSERT_EQUAL_UINT8(1, n);
	TEST_ASSERT_EQUAL_UINT8(EVENT_TEMP_HIGH, events[0].type);

	n = 0;
	app_alarms_eval(&data, &config, 0, &flags, events, &n);
	TEST_ASSERT_EQUAL_UINT8(0, n);
	TEST_ASSERT_BITS_HIGH(ALARM_TEMP_HIGH, flags);
}

void test_alarms_temp_clears_in_window(void)
{
	tag_config_t config = cfg();
	sensor_data_t data = { 0 };
	struct app_alarm_event events[APP_ALARM_MAX_EVENTS];
	uint8_t n = 0;
	uint16_t flags = ALARM_TEMP_HIGH | ALARM_TEMP_LOW;

	data.valid = SENSOR_VALID_TEMP_RH;
	data.temperature_c_x100 = 2000;

	app_alarms_eval(&data, &config, 0, &flags, events, &n);
	TEST_ASSERT_BITS_LOW(ALARM_TEMP_HIGH | ALARM_TEMP_LOW, flags);
	TEST_ASSERT_EQUAL_UINT8(0, n);
}

void test_alarms_battery_needs_hysteresis_to_clear(void)
{
	tag_config_t config = cfg();
	sensor_data_t data = { 0 };
	struct app_alarm_event events[APP_ALARM_MAX_EVENTS];
	uint8_t n = 0;
	uint16_t flags = 0;

	data.valid = SENSOR_VALID_BATTERY;
	data.battery_mv = 2300; /* default low is 2400 */

	app_alarms_eval(&data, &config, 0, &flags, events, &n);
	TEST_ASSERT_BITS_HIGH(ALARM_BATTERY_LOW, flags);
	TEST_ASSERT_EQUAL_UINT8(EVENT_BATTERY_LOW, events[0].type);

	data.battery_mv = 2450; /* still inside 100 mV hysteresis */
	n = 0;
	app_alarms_eval(&data, &config, 0, &flags, events, &n);
	TEST_ASSERT_BITS_HIGH(ALARM_BATTERY_LOW, flags);

	data.battery_mv = 2500; /* 2400 + 100 */
	n = 0;
	app_alarms_eval(&data, &config, 0, &flags, events, &n);
	TEST_ASSERT_BITS_LOW(ALARM_BATTERY_LOW, flags);
}

void test_alarms_gas_zero_threshold_disabled(void)
{
	tag_config_t config = cfg();
	sensor_data_t data = { 0 };
	struct app_alarm_event events[APP_ALARM_MAX_EVENTS];
	uint8_t n = 0;
	uint16_t flags = 0;

	data.valid = SENSOR_VALID_GAS;
	data.gas_resistance_ohm = 1;
	config.gas_low_threshold_ohm = 0;

	app_alarms_eval(&data, &config, 0, &flags, events, &n);
	TEST_ASSERT_EQUAL_UINT16(0, flags);
	TEST_ASSERT_EQUAL_UINT8(0, n);
}

void test_alarms_gas_floor_fires_and_clears(void)
{
	tag_config_t config = cfg();
	sensor_data_t data = { 0 };
	struct app_alarm_event events[APP_ALARM_MAX_EVENTS];
	uint8_t n = 0;
	uint16_t flags = 0;

	config.gas_low_threshold_ohm = 50000;
	data.valid = SENSOR_VALID_GAS;
	data.gas_resistance_ohm = 40000;

	app_alarms_eval(&data, &config, 0, &flags, events, &n);
	TEST_ASSERT_BITS_HIGH(ALARM_GAS_LOW, flags);
	TEST_ASSERT_EQUAL_UINT8(EVENT_GAS_LOW, events[0].type);
	TEST_ASSERT_EQUAL_INT16(40, events[0].value); /* kOhm */

	data.gas_resistance_ohm = 60000;
	n = 0;
	app_alarms_eval(&data, &config, 0, &flags, events, &n);
	TEST_ASSERT_BITS_LOW(ALARM_GAS_LOW, flags);
}

void test_alarms_sensor_fault_is_subset_not_equality(void)
{
	tag_config_t config = cfg();
	sensor_data_t data = { 0 };
	struct app_alarm_event events[APP_ALARM_MAX_EVENTS];
	uint8_t n = 0;
	uint16_t flags = 0;
	uint8_t expected = SENSOR_VALID_TEMP_RH | SENSOR_VALID_PRESSURE |
			   SENSOR_VALID_ACCEL | SENSOR_VALID_BATTERY |
			   SENSOR_VALID_GAS;

	data.valid = expected | SENSOR_VALID_GYRO;
	app_alarms_eval(&data, &config, expected, &flags, events, &n);
	TEST_ASSERT_BITS_LOW(ALARM_SENSOR_FAULT, flags);

	data.valid = SENSOR_VALID_TEMP_RH;
	n = 0;
	app_alarms_eval(&data, &config, expected, &flags, events, &n);
	TEST_ASSERT_BITS_HIGH(ALARM_SENSOR_FAULT, flags);
	TEST_ASSERT_EQUAL_UINT8(EVENT_SENSOR_FAULT, events[0].type);
}

void test_alarms_humidity_high(void)
{
	tag_config_t config = cfg();
	sensor_data_t data = { 0 };
	struct app_alarm_event events[APP_ALARM_MAX_EVENTS];
	uint8_t n = 0;
	uint16_t flags = 0;

	data.valid = SENSOR_VALID_TEMP_RH;
	data.temperature_c_x100 = 2000;
	data.humidity_x100 = 8500; /* default high 80 % */

	app_alarms_eval(&data, &config, 0, &flags, events, &n);
	TEST_ASSERT_BITS_HIGH(ALARM_HUMIDITY_HIGH, flags);
	TEST_ASSERT_EQUAL_UINT8(EVENT_HUMIDITY_HIGH, events[0].type);
}
