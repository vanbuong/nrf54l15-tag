#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_classifier_rest_is_inactive(void);
void test_classifier_active_when_above_threshold(void);
void test_classifier_not_active_at_threshold(void);
void test_classifier_negative_deviation(void);
void test_classifier_z_up(void);
void test_classifier_z_down(void);
void test_classifier_y_up(void);
void test_classifier_x_down(void);
void test_classifier_unknown_when_tumbling(void);

void test_shock_fires_once_per_impact(void);
void test_shock_tracks_peak(void);
void test_free_fall_fires_once_until_rearm(void);
void test_rest_is_quiet(void);

void test_reporting_first_sample_sends_all(void);
void test_reporting_below_delta_is_quiet(void);
void test_reporting_temperature_delta(void);
void test_reporting_humidity_pressure_battery_deltas(void);
void test_reporting_heartbeat_on_max_interval(void);
void test_reporting_motion_is_immediate(void);
void test_reporting_gas_delta(void);
void test_reporting_gas_disabled_when_delta_zero(void);
void test_reporting_force_resets_baseline(void);
void test_reporting_commit_zero_is_noop(void);

void test_config_defaults_are_valid(void);
void test_config_rejects_zero_interval(void);
void test_config_rejects_huge_interval(void);
void test_config_rejects_inverted_temp_window(void);
void test_config_rejects_humidity_over_100(void);
void test_config_rejects_bad_mode(void);
void test_config_rejects_non_boolean_flags(void);
void test_config_rejects_out_of_range_thresholds(void);
void test_config_set_rejects_and_keeps_previous(void);
void test_config_effective_interval_by_mode(void);
void test_config_reset_restores_defaults(void);

void test_flash_append_and_read(void);
void test_flash_get_oldest(void);
void test_flash_torn_write_skipped_on_rescan(void);
void test_flash_erase_resets_sequence(void);
void test_flash_wrap_drops_oldest_sector(void);
void test_flash_rejects_oversized_payload(void);
void test_flash_unready_returns_enodev(void);

void test_battery_percent_full(void);
void test_battery_percent_empty(void);
void test_battery_percent_mid(void);
void test_sensor_data_packed_size(void);
void test_log_record_from_sample(void);
void test_gas_alarm_floor(void);
void test_boot_epoch_helpers(void);

void test_config_roundtrip(void);
void test_config_v0_blob_loads(void);
void test_config_short_schema_body_keeps_new_defaults(void);
void test_config_rejects_unknown_schema(void);
void test_config_rejects_truncated_header(void);
void test_stats_v0_sets_epoch_zero(void);
void test_stats_roundtrip_keeps_epoch(void);
void test_encode_rejects_undersized_buffer(void);
void test_stats_rejects_bad_framed_blobs(void);

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_classifier_rest_is_inactive);
	RUN_TEST(test_classifier_active_when_above_threshold);
	RUN_TEST(test_classifier_not_active_at_threshold);
	RUN_TEST(test_classifier_negative_deviation);
	RUN_TEST(test_classifier_z_up);
	RUN_TEST(test_classifier_z_down);
	RUN_TEST(test_classifier_y_up);
	RUN_TEST(test_classifier_x_down);
	RUN_TEST(test_classifier_unknown_when_tumbling);

	RUN_TEST(test_shock_fires_once_per_impact);
	RUN_TEST(test_shock_tracks_peak);
	RUN_TEST(test_free_fall_fires_once_until_rearm);
	RUN_TEST(test_rest_is_quiet);

	RUN_TEST(test_reporting_first_sample_sends_all);
	RUN_TEST(test_reporting_below_delta_is_quiet);
	RUN_TEST(test_reporting_temperature_delta);
	RUN_TEST(test_reporting_humidity_pressure_battery_deltas);
	RUN_TEST(test_reporting_heartbeat_on_max_interval);
	RUN_TEST(test_reporting_motion_is_immediate);
	RUN_TEST(test_reporting_gas_delta);
	RUN_TEST(test_reporting_gas_disabled_when_delta_zero);
	RUN_TEST(test_reporting_force_resets_baseline);
	RUN_TEST(test_reporting_commit_zero_is_noop);

	RUN_TEST(test_config_defaults_are_valid);
	RUN_TEST(test_config_rejects_zero_interval);
	RUN_TEST(test_config_rejects_huge_interval);
	RUN_TEST(test_config_rejects_inverted_temp_window);
	RUN_TEST(test_config_rejects_humidity_over_100);
	RUN_TEST(test_config_rejects_bad_mode);
	RUN_TEST(test_config_rejects_non_boolean_flags);
	RUN_TEST(test_config_rejects_out_of_range_thresholds);
	RUN_TEST(test_config_set_rejects_and_keeps_previous);
	RUN_TEST(test_config_effective_interval_by_mode);
	RUN_TEST(test_config_reset_restores_defaults);

	RUN_TEST(test_flash_append_and_read);
	RUN_TEST(test_flash_get_oldest);
	RUN_TEST(test_flash_torn_write_skipped_on_rescan);
	RUN_TEST(test_flash_erase_resets_sequence);
	RUN_TEST(test_flash_wrap_drops_oldest_sector);
	RUN_TEST(test_flash_rejects_oversized_payload);
	RUN_TEST(test_flash_unready_returns_enodev);

	RUN_TEST(test_battery_percent_full);
	RUN_TEST(test_battery_percent_empty);
	RUN_TEST(test_battery_percent_mid);
	RUN_TEST(test_sensor_data_packed_size);
	RUN_TEST(test_log_record_from_sample);
	RUN_TEST(test_gas_alarm_floor);
	RUN_TEST(test_boot_epoch_helpers);

	RUN_TEST(test_config_roundtrip);
	RUN_TEST(test_config_v0_blob_loads);
	RUN_TEST(test_config_short_schema_body_keeps_new_defaults);
	RUN_TEST(test_config_rejects_unknown_schema);
	RUN_TEST(test_config_rejects_truncated_header);
	RUN_TEST(test_stats_v0_sets_epoch_zero);
	RUN_TEST(test_stats_roundtrip_keeps_epoch);
	RUN_TEST(test_encode_rejects_undersized_buffer);
	RUN_TEST(test_stats_rejects_bad_framed_blobs);

	return UNITY_END();
}
