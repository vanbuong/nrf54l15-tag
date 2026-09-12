#include "unity.h"
#include "app/app_config.h"

#include <errno.h>

void config_storage_stub_reset(void);

void test_config_defaults_are_valid(void)
{
	tag_config_t cfg = TAG_CONFIG_DEFAULTS;

	TEST_ASSERT_TRUE(app_config_validate(&cfg));
}

void test_config_rejects_zero_interval(void)
{
	tag_config_t cfg = TAG_CONFIG_DEFAULTS;

	cfg.sampling_interval_s = 0;
	TEST_ASSERT_FALSE(app_config_validate(&cfg));
}

void test_config_rejects_huge_interval(void)
{
	tag_config_t cfg = TAG_CONFIG_DEFAULTS;

	cfg.sampling_interval_s = 3601;
	TEST_ASSERT_FALSE(app_config_validate(&cfg));
}

void test_config_rejects_inverted_temp_window(void)
{
	tag_config_t cfg = TAG_CONFIG_DEFAULTS;

	cfg.temp_high_c_x100 = 0;
	cfg.temp_low_c_x100 = 100;
	TEST_ASSERT_FALSE(app_config_validate(&cfg));
}

void test_config_rejects_humidity_over_100(void)
{
	tag_config_t cfg = TAG_CONFIG_DEFAULTS;

	cfg.humidity_high_x100 = 10001;
	TEST_ASSERT_FALSE(app_config_validate(&cfg));
}

void test_config_rejects_bad_mode(void)
{
	tag_config_t cfg = TAG_CONFIG_DEFAULTS;

	cfg.operating_mode = 3;
	TEST_ASSERT_FALSE(app_config_validate(&cfg));
}

void test_config_rejects_non_boolean_flags(void)
{
	tag_config_t cfg = TAG_CONFIG_DEFAULTS;

	cfg.led_enabled = 2;
	TEST_ASSERT_FALSE(app_config_validate(&cfg));
}

void test_config_rejects_out_of_range_thresholds(void)
{
	tag_config_t cfg = TAG_CONFIG_DEFAULTS;

	cfg.motion_timeout_s = 0;
	TEST_ASSERT_FALSE(app_config_validate(&cfg));
	cfg = (tag_config_t)TAG_CONFIG_DEFAULTS;
	cfg.motion_threshold_mg = 10;
	TEST_ASSERT_FALSE(app_config_validate(&cfg));
	cfg = (tag_config_t)TAG_CONFIG_DEFAULTS;
	cfg.shock_threshold_mg = 100;
	TEST_ASSERT_FALSE(app_config_validate(&cfg));
	cfg = (tag_config_t)TAG_CONFIG_DEFAULTS;
	cfg.free_fall_threshold_mg = 901;
	TEST_ASSERT_FALSE(app_config_validate(&cfg));
	cfg = (tag_config_t)TAG_CONFIG_DEFAULTS;
	cfg.battery_low_mv = 1000;
	TEST_ASSERT_FALSE(app_config_validate(&cfg));
	cfg = (tag_config_t)TAG_CONFIG_DEFAULTS;
	cfg.report_max_interval_s = 30;
	TEST_ASSERT_FALSE(app_config_validate(&cfg));
	cfg = (tag_config_t)TAG_CONFIG_DEFAULTS;
	cfg.ble_service_window_s = 5;
	TEST_ASSERT_FALSE(app_config_validate(&cfg));
}

void test_config_set_rejects_and_keeps_previous(void)
{
	tag_config_t cfg;
	tag_config_t bad = TAG_CONFIG_DEFAULTS;

	config_storage_stub_reset();
	TEST_ASSERT_EQUAL_INT(0, app_config_init());

	bad.sampling_interval_s = 0;
	TEST_ASSERT_EQUAL_INT(-EINVAL, app_config_set(&bad));

	app_config_get(&cfg);
	TEST_ASSERT_EQUAL_UINT16(60, cfg.sampling_interval_s);
}

void test_config_effective_interval_by_mode(void)
{
	config_storage_stub_reset();
	TEST_ASSERT_EQUAL_INT(0, app_config_init());

	TEST_ASSERT_EQUAL_UINT16(60, app_config_effective_interval_s());

	TEST_ASSERT_EQUAL_INT(0, app_config_set_mode(TAG_MODE_ASSET));
	TEST_ASSERT_EQUAL_UINT16(30, app_config_effective_interval_s());

	TEST_ASSERT_EQUAL_INT(0, app_config_set_mode(TAG_MODE_SHIPPING));
	TEST_ASSERT_EQUAL_UINT16(15, app_config_effective_interval_s());
}

void test_config_reset_restores_defaults(void)
{
	config_storage_stub_reset();
	TEST_ASSERT_EQUAL_INT(0, app_config_init());
	TEST_ASSERT_EQUAL_INT(0, app_config_set_sampling_interval(120));
	TEST_ASSERT_EQUAL_INT(0, app_config_reset());
	TEST_ASSERT_EQUAL_UINT16(60, app_config_effective_interval_s());
}
