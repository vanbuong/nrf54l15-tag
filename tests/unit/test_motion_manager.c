#include "unity.h"
#include "motion/motion_manager.h"

static tag_config_t cfg(void)
{
	return (tag_config_t)TAG_CONFIG_DEFAULTS;
}

static sensor_data_t accel(uint32_t ts, int16_t x, int16_t y, int16_t z,
			   uint16_t mag)
{
	sensor_data_t data = { 0 };

	data.timestamp = ts;
	data.accel_x_mg = x;
	data.accel_y_mg = y;
	data.accel_z_mg = z;
	data.magnitude_mg = mag;
	data.valid = SENSOR_VALID_ACCEL;
	return data;
}

void test_motion_start_and_stop_after_timeout(void)
{
	tag_config_t config = cfg();
	struct motion_result r;
	sensor_data_t data;

	motion_manager_init(&config);

	data = accel(0, 0, 0, 980, 1000);
	motion_manager_process(&data, &r);
	TEST_ASSERT_EQUAL_UINT32(0, r.events & MOTION_EVENT_START);
	TEST_ASSERT_EQUAL_UINT8(MOTION_STATE_STATIONARY, r.motion_state);

	data = accel(1, 0, 0, 980, 1200); /* 200 mg above 1 g, threshold 150 */
	motion_manager_process(&data, &r);
	TEST_ASSERT_TRUE(r.events & MOTION_EVENT_START);
	TEST_ASSERT_EQUAL_UINT8(MOTION_STATE_MOVING, r.motion_state);
	TEST_ASSERT_EQUAL_INT(MOTION_STATE_MOVING, motion_manager_state());
	TEST_ASSERT_TRUE(data.motion);

	data = accel(5, 0, 0, 980, 1200);
	motion_manager_process(&data, &r);
	TEST_ASSERT_EQUAL_UINT8(MOTION_STATE_MOVING, r.motion_state);

	data = accel(10, 0, 0, 980, 1000);
	motion_manager_process(&data, &r);
	TEST_ASSERT_EQUAL_UINT8(MOTION_STATE_MOVING, r.motion_state);
	TEST_ASSERT_EQUAL_UINT32(0, r.events & MOTION_EVENT_STOP);

	data = accel(5 + config.motion_timeout_s, 0, 0, 980, 1000);
	motion_manager_process(&data, &r);
	TEST_ASSERT_TRUE(r.events & MOTION_EVENT_STOP);
	TEST_ASSERT_EQUAL_UINT8(MOTION_STATE_STATIONARY, r.motion_state);
	TEST_ASSERT_EQUAL_UINT32(4, r.movement_duration_s);
	TEST_ASSERT_EQUAL_UINT32(4, motion_manager_total_movement_s());
	TEST_ASSERT_EQUAL_INT(ORIENTATION_Z_UP, motion_manager_orientation());
}

void test_tamper_after_settled_shock_and_reorient(void)
{
	tag_config_t config = cfg();
	struct motion_result r;
	sensor_data_t data;

	motion_manager_init(&config);

	data = accel(0, 10, 20, 980, 1000);
	motion_manager_process(&data, &r);
	TEST_ASSERT_EQUAL_UINT8(ORIENTATION_Z_UP, r.orientation);
	TEST_ASSERT_EQUAL_UINT32(0, r.events & MOTION_EVENT_TAMPER);

	data = accel(300, 900, 10, 20, 6000); /* settled, shock, X_UP */
	motion_manager_process(&data, &r);
	TEST_ASSERT_TRUE(r.events & MOTION_EVENT_SHOCK);
	TEST_ASSERT_TRUE(r.events & MOTION_EVENT_ORIENTATION);
	TEST_ASSERT_TRUE(r.events & MOTION_EVENT_TAMPER);
	TEST_ASSERT_TRUE(motion_manager_tamper());

	motion_manager_clear_tamper();
	TEST_ASSERT_FALSE(motion_manager_tamper());
}

void test_shock_without_settle_is_not_tamper(void)
{
	tag_config_t config = cfg();
	struct motion_result r;
	sensor_data_t data;

	motion_manager_init(&config);

	data = accel(0, 10, 20, 980, 1000);
	motion_manager_process(&data, &r);

	data = accel(10, 900, 10, 20, 6000); /* only 10 s at rest */
	motion_manager_process(&data, &r);
	TEST_ASSERT_TRUE(r.events & MOTION_EVENT_SHOCK);
	TEST_ASSERT_EQUAL_UINT32(0, r.events & MOTION_EVENT_TAMPER);
}

void test_reorient_without_shock_is_not_tamper(void)
{
	tag_config_t config = cfg();
	struct motion_result r;
	sensor_data_t data;

	motion_manager_init(&config);

	data = accel(0, 10, 20, 980, 1000);
	motion_manager_process(&data, &r);

	data = accel(300, 900, 10, 20, 1000); /* settled, flip, no shock */
	motion_manager_process(&data, &r);
	TEST_ASSERT_TRUE(r.events & MOTION_EVENT_ORIENTATION);
	TEST_ASSERT_EQUAL_UINT32(0, r.events & MOTION_EVENT_TAMPER);
	TEST_ASSERT_EQUAL_UINT32(0, r.events & MOTION_EVENT_SHOCK);
}
