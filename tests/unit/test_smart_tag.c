#include "unity.h"
#include "app/smart_tag.h"

void test_battery_percent_full(void)
{
	TEST_ASSERT_EQUAL_UINT8(100, smart_tag_battery_percent(3000));
	TEST_ASSERT_EQUAL_UINT8(100, smart_tag_battery_percent(3300));
}

void test_battery_percent_empty(void)
{
	TEST_ASSERT_EQUAL_UINT8(0, smart_tag_battery_percent(2000));
	TEST_ASSERT_EQUAL_UINT8(0, smart_tag_battery_percent(1500));
}

void test_battery_percent_mid(void)
{
	TEST_ASSERT_EQUAL_UINT8(50, smart_tag_battery_percent(2500));
}

void test_sensor_data_packed_size(void)
{
	/*
	 * Protocol v3 BLE Sensor Data payload. A silent layout change
	 * would desynchronise every phone decoder — fail the build.
	 */
	TEST_ASSERT_EQUAL_UINT32(38, sizeof(sensor_data_t));
	TEST_ASSERT_EQUAL_UINT32(17, sizeof(sensor_log_record_t));
	TEST_ASSERT_EQUAL_UINT32(8, sizeof(event_record_t));
}

void test_log_record_from_sample(void)
{
	sensor_data_t data = { 0 };
	sensor_log_record_t rec;

	data.timestamp = 42;
	data.temperature_c_x100 = 2150;
	data.humidity_x100 = 4000;
	data.pressure_pa = 101325;
	data.gas_resistance_ohm = 80000;
	data.valid = SENSOR_VALID_TEMP_RH | SENSOR_VALID_PRESSURE |
		     SENSOR_VALID_GAS;
	data.motion = true;

	smart_tag_fill_log_record(&rec, &data, true);

	TEST_ASSERT_EQUAL_UINT32(42, rec.timestamp);
	TEST_ASSERT_EQUAL_INT16(2150, rec.temperature);
	TEST_ASSERT_EQUAL_UINT32(80000, rec.gas_resistance_ohm);
	TEST_ASSERT_BITS_HIGH(SENSOR_FLAG_MOVING, rec.flags);
	TEST_ASSERT_BITS_HIGH(SENSOR_FLAG_ALARM, rec.flags);
	TEST_ASSERT_BITS_HIGH(SENSOR_VALID_GAS, rec.flags);
}
