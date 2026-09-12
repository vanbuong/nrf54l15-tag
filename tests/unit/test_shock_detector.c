#include "unity.h"
#include "motion/shock_detector.h"

static sensor_data_t sample(uint16_t mag)
{
	sensor_data_t data = { 0 };

	data.magnitude_mg = mag;
	data.valid = SENSOR_VALID_ACCEL;
	return data;
}

void test_shock_fires_once_per_impact(void)
{
	struct shock_result r;
	sensor_data_t rest = sample(1000);
	sensor_data_t hit = sample(1000 + 5000);

	shock_detector_init();
	shock_detector_process(&hit, 4000, 300, &r);
	TEST_ASSERT_TRUE(r.shock);
	TEST_ASSERT_EQUAL_UINT16(5000, r.peak_mg);

	shock_detector_process(&hit, 4000, 300, &r);
	TEST_ASSERT_FALSE(r.shock);

	shock_detector_process(&rest, 4000, 300, &r);
	TEST_ASSERT_FALSE(r.shock);

	shock_detector_process(&hit, 4000, 300, &r);
	TEST_ASSERT_TRUE(r.shock);
}

void test_shock_tracks_peak(void)
{
	struct shock_result r;

	shock_detector_init();
	shock_detector_process(&(sensor_data_t){ .magnitude_mg = 6000 }, 4000,
			       300, &r);
	TEST_ASSERT_EQUAL_UINT16(5000, shock_detector_peak());

	shock_detector_reset_peak();
	TEST_ASSERT_EQUAL_UINT16(0, shock_detector_peak());
}

void test_free_fall_fires_once_until_rearm(void)
{
	struct shock_result r;
	sensor_data_t fall = sample(50);
	sensor_data_t rest = sample(1000);

	shock_detector_init();
	shock_detector_process(&fall, 4000, 300, &r);
	TEST_ASSERT_TRUE(r.free_fall);

	shock_detector_process(&fall, 4000, 300, &r);
	TEST_ASSERT_FALSE(r.free_fall);

	/* Still in the hysteresis band - does not re-arm. */
	shock_detector_process(&(sensor_data_t){ .magnitude_mg = 400 }, 4000,
			       300, &r);
	TEST_ASSERT_FALSE(r.free_fall);

	shock_detector_process(&rest, 4000, 300, &r);
	shock_detector_process(&fall, 4000, 300, &r);
	TEST_ASSERT_TRUE(r.free_fall);
}

void test_rest_is_quiet(void)
{
	struct shock_result r;

	shock_detector_init();
	shock_detector_process(&(sensor_data_t){ .magnitude_mg = 1000 }, 4000,
			       300, &r);
	TEST_ASSERT_FALSE(r.shock);
	TEST_ASSERT_FALSE(r.free_fall);
	TEST_ASSERT_EQUAL_UINT16(0, r.peak_mg);
}
