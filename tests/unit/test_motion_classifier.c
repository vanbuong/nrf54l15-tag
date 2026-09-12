#include "unity.h"
#include "motion/motion_classifier.h"

static sensor_data_t sample(int16_t x, int16_t y, int16_t z, uint16_t mag)
{
	sensor_data_t data = { 0 };

	data.accel_x_mg = x;
	data.accel_y_mg = y;
	data.accel_z_mg = z;
	data.magnitude_mg = mag;
	data.valid = SENSOR_VALID_ACCEL;
	return data;
}

void test_classifier_rest_is_inactive(void)
{
	sensor_data_t data = sample(0, 0, 1000, 1000);

	TEST_ASSERT_EQUAL_INT32(0, motion_classifier_deviation(&data));
	TEST_ASSERT_FALSE(motion_classifier_is_active(&data, 150));
}

void test_classifier_active_when_above_threshold(void)
{
	sensor_data_t data = sample(0, 0, 1000, 1200);

	TEST_ASSERT_TRUE(motion_classifier_is_active(&data, 150));
}

void test_classifier_not_active_at_threshold(void)
{
	sensor_data_t data = sample(0, 0, 1000, 1150);

	/* is_active uses > threshold, not >= */
	TEST_ASSERT_FALSE(motion_classifier_is_active(&data, 150));
}

void test_classifier_negative_deviation(void)
{
	sensor_data_t data = sample(0, 0, 0, 200);

	TEST_ASSERT_EQUAL_INT32(-800, motion_classifier_deviation(&data));
	TEST_ASSERT_TRUE(motion_classifier_is_active(&data, 150));
}

void test_classifier_z_up(void)
{
	sensor_data_t data = sample(10, 20, 980, 1000);

	TEST_ASSERT_EQUAL_INT(ORIENTATION_Z_UP,
			      motion_classifier_orientation(&data));
}

void test_classifier_z_down(void)
{
	sensor_data_t data = sample(10, 20, -980, 1000);

	TEST_ASSERT_EQUAL_INT(ORIENTATION_Z_DOWN,
			      motion_classifier_orientation(&data));
}

void test_classifier_y_up(void)
{
	sensor_data_t data = sample(10, 900, 20, 1000);

	TEST_ASSERT_EQUAL_INT(ORIENTATION_Y_UP,
			      motion_classifier_orientation(&data));
}

void test_classifier_x_down(void)
{
	sensor_data_t data = sample(-900, 10, 20, 1000);

	TEST_ASSERT_EQUAL_INT(ORIENTATION_X_DOWN,
			      motion_classifier_orientation(&data));
}

void test_classifier_unknown_when_tumbling(void)
{
	sensor_data_t data = sample(100, 100, 100, 173);

	TEST_ASSERT_EQUAL_INT(ORIENTATION_UNKNOWN,
			      motion_classifier_orientation(&data));
}
