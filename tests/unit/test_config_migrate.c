#include "unity.h"
#include "storage/config_migrate.h"

#include <errno.h>
#include <string.h>

void test_config_roundtrip(void)
{
	tag_config_t in = TAG_CONFIG_DEFAULTS;
	tag_config_t out;
	uint8_t blob[64];
	size_t n;

	in.sampling_interval_s = 90;
	in.gas_low_threshold_ohm = 50000;

	n = tag_config_encode(&in, blob, sizeof(blob));
	TEST_ASSERT_EQUAL_UINT32(TAG_CONFIG_FRAMED_SIZE, n);
	TEST_ASSERT_EQUAL_INT(0, tag_config_decode(blob, n, &out));
	TEST_ASSERT_EQUAL_UINT16(90, out.sampling_interval_s);
	TEST_ASSERT_EQUAL_UINT32(50000, out.gas_low_threshold_ohm);
	TEST_ASSERT_EQUAL_UINT8(in.operating_mode, out.operating_mode);
}

void test_config_v0_blob_loads(void)
{
	tag_config_t v0 = TAG_CONFIG_DEFAULTS;
	tag_config_t out;
	uint8_t blob[TAG_CONFIG_V0_SIZE];

	v0.sampling_interval_s = 15;
	v0.led_enabled = 0;
	memcpy(blob, &v0, sizeof(blob));

	TEST_ASSERT_EQUAL_INT(0, tag_config_decode(blob, sizeof(blob), &out));
	TEST_ASSERT_EQUAL_UINT16(15, out.sampling_interval_s);
	TEST_ASSERT_EQUAL_UINT8(0, out.led_enabled);
	TEST_ASSERT_EQUAL_UINT16(v0.motion_threshold_mg, out.motion_threshold_mg);
}

void test_config_short_schema_body_keeps_new_defaults(void)
{
	uint8_t blob[8];
	tag_config_t out;
	struct tag_settings_header header = {
		.schema = TAG_CONFIG_SCHEMA,
		.body_size = 2,
	};

	memcpy(blob, &header, sizeof(header));
	blob[4] = 30; /* sampling_interval_s little-endian */
	blob[5] = 0;

	TEST_ASSERT_EQUAL_INT(0, tag_config_decode(blob, sizeof(blob), &out));
	TEST_ASSERT_EQUAL_UINT16(30, out.sampling_interval_s);
	TEST_ASSERT_EQUAL_UINT16(30, out.motion_timeout_s);
	TEST_ASSERT_EQUAL_UINT8(1, out.led_enabled);
}

void test_config_rejects_unknown_schema(void)
{
	uint8_t blob[8];
	tag_config_t out;
	struct tag_settings_header header = {
		.schema = 99,
		.body_size = 2,
	};

	memcpy(blob, &header, sizeof(header));
	TEST_ASSERT_EQUAL_INT(-EINVAL, tag_config_decode(blob, sizeof(blob), &out));
}

void test_config_rejects_truncated_header(void)
{
	uint8_t blob[3] = { 1, 0, 40 };
	tag_config_t out;

	TEST_ASSERT_EQUAL_INT(-EINVAL, tag_config_decode(blob, sizeof(blob), &out));
	TEST_ASSERT_EQUAL_INT(-EINVAL, tag_config_decode(NULL, 8, &out));
	TEST_ASSERT_EQUAL_INT(-EINVAL, tag_config_decode(blob, 8, NULL));
}

void test_stats_v0_sets_epoch_zero(void)
{
	uint8_t blob[TAG_STATS_V0_SIZE];
	tag_statistics_t out;
	struct {
		uint32_t boot_count;
		uint32_t total_uptime_s;
		uint32_t motion_count;
		uint32_t shock_count;
		uint32_t free_fall_count;
		uint32_t event_count;
		uint32_t movement_duration_s;
		uint16_t max_shock_mg;
		uint16_t min_battery_mv;
		uint16_t zigbee_reports;
		uint16_t ble_connections;
		uint16_t sensor_faults;
		uint16_t reserved;
	} __packed v0 = {
		.boot_count = 7,
		.total_uptime_s = 1234,
		.shock_count = 3,
		.max_shock_mg = 4500,
		.min_battery_mv = 2100,
		.sensor_faults = 1,
		.reserved = 0xABCD,
	};

	memset(blob, 0, sizeof(blob));
	memcpy(blob, &v0, sizeof(v0));

	TEST_ASSERT_EQUAL_INT(0, tag_stats_decode(blob, sizeof(blob), &out));
	TEST_ASSERT_EQUAL_UINT32(7, out.boot_count);
	TEST_ASSERT_EQUAL_UINT32(1234, out.total_uptime_s);
	TEST_ASSERT_EQUAL_UINT32(3, out.shock_count);
	TEST_ASSERT_EQUAL_UINT16(4500, out.max_shock_mg);
	TEST_ASSERT_EQUAL_UINT16(1, out.sensor_faults);
	TEST_ASSERT_EQUAL_UINT32(0, out.boot_epoch_utc);
}

void test_stats_roundtrip_keeps_epoch(void)
{
	tag_statistics_t in = { 0 };
	tag_statistics_t out;
	uint8_t blob[64];
	size_t n;

	in.boot_count = 4;
	in.boot_epoch_utc = 1700000000;

	n = tag_stats_encode(&in, blob, sizeof(blob));
	TEST_ASSERT_EQUAL_UINT32(TAG_STATS_FRAMED_SIZE, n);
	TEST_ASSERT_EQUAL_INT(0, tag_stats_decode(blob, n, &out));
	TEST_ASSERT_EQUAL_UINT32(4, out.boot_count);
	TEST_ASSERT_EQUAL_UINT32(1700000000, out.boot_epoch_utc);
}

void test_encode_rejects_undersized_buffer(void)
{
	tag_config_t cfg = TAG_CONFIG_DEFAULTS;
	uint8_t tiny[4];

	TEST_ASSERT_EQUAL_UINT32(0, tag_config_encode(&cfg, tiny, sizeof(tiny)));
	TEST_ASSERT_EQUAL_UINT32(0, tag_config_encode(NULL, tiny, 64));
	TEST_ASSERT_EQUAL_UINT32(0, tag_stats_encode(NULL, tiny, 64));
}

void test_stats_rejects_bad_framed_blobs(void)
{
	tag_statistics_t out;
	uint8_t blob[8];
	struct tag_settings_header header = {
		.schema = 99,
		.body_size = 2,
	};

	memcpy(blob, &header, sizeof(header));
	TEST_ASSERT_EQUAL_INT(-EINVAL, tag_stats_decode(blob, sizeof(blob), &out));

	header.schema = TAG_STATS_SCHEMA;
	header.body_size = 100;
	memcpy(blob, &header, sizeof(header));
	TEST_ASSERT_EQUAL_INT(-EINVAL, tag_stats_decode(blob, sizeof(blob), &out));

	TEST_ASSERT_EQUAL_INT(-EINVAL, tag_stats_decode(blob, 2, &out));
	TEST_ASSERT_EQUAL_INT(-EINVAL, tag_stats_decode(NULL, 8, &out));
}
