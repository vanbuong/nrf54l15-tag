#include <errno.h>
#include <string.h>

#include "config_migrate.h"

struct tag_stats_v0 {
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
} __packed;

static size_t encode(uint16_t schema, const void *body, uint16_t body_size,
		     void *blob, size_t max)
{
	struct tag_settings_header header = {
		.schema = schema,
		.body_size = body_size,
	};

	if (blob == NULL || max < sizeof(header) + body_size) {
		return 0;
	}

	memcpy(blob, &header, sizeof(header));
	memcpy((uint8_t *)blob + sizeof(header), body, body_size);
	return sizeof(header) + body_size;
}

static int copy_onto_defaults(void *out, size_t out_size, const void *body,
			      size_t body_size, const void *defaults)
{
	if (body == NULL || out == NULL) {
		return -EINVAL;
	}

	memcpy(out, defaults, out_size);
	memcpy(out, body, MIN(body_size, out_size));
	return 0;
}

int tag_config_decode(const void *blob, size_t len, tag_config_t *out)
{
	tag_config_t defaults = TAG_CONFIG_DEFAULTS;
	const struct tag_settings_header *header;

	if (out == NULL || blob == NULL || len == 0U) {
		return -EINVAL;
	}

	/* Protocol v3: the body was stored with no header. */
	if (len == TAG_CONFIG_V0_SIZE) {
		return copy_onto_defaults(out, sizeof(*out), blob, len,
					  &defaults);
	}

	if (len < sizeof(*header)) {
		return -EINVAL;
	}

	header = blob;

	if (header->schema == 0U || header->schema > TAG_CONFIG_SCHEMA) {
		return -EINVAL;
	}

	if (sizeof(*header) + header->body_size > len) {
		return -EINVAL;
	}

	return copy_onto_defaults(out, sizeof(*out),
				  (const uint8_t *)blob + sizeof(*header),
				  header->body_size, &defaults);
}

size_t tag_config_encode(const tag_config_t *in, void *blob, size_t max)
{
	if (in == NULL) {
		return 0;
	}

	return encode(TAG_CONFIG_SCHEMA, in, (uint16_t)sizeof(*in), blob, max);
}

int tag_stats_decode(const void *blob, size_t len, tag_statistics_t *out)
{
	tag_statistics_t defaults;
	const struct tag_settings_header *header;

	if (out == NULL || blob == NULL || len == 0U) {
		return -EINVAL;
	}

	memset(&defaults, 0, sizeof(defaults));

	/* Protocol v3: 40-byte struct with a trailing reserved uint16. */
	if (len == TAG_STATS_V0_SIZE) {
		struct tag_stats_v0 v0;

		memset(&v0, 0, sizeof(v0));
		memcpy(&v0, blob, MIN(len, sizeof(v0)));
		memset(out, 0, sizeof(*out));
		out->boot_count = v0.boot_count;
		out->total_uptime_s = v0.total_uptime_s;
		out->motion_count = v0.motion_count;
		out->shock_count = v0.shock_count;
		out->free_fall_count = v0.free_fall_count;
		out->event_count = v0.event_count;
		out->movement_duration_s = v0.movement_duration_s;
		out->max_shock_mg = v0.max_shock_mg;
		out->min_battery_mv = v0.min_battery_mv;
		out->zigbee_reports = v0.zigbee_reports;
		out->ble_connections = v0.ble_connections;
		out->sensor_faults = v0.sensor_faults;
		out->boot_epoch_utc = 0;
		return 0;
	}

	if (len < sizeof(*header)) {
		return -EINVAL;
	}

	header = blob;

	if (header->schema == 0U || header->schema > TAG_STATS_SCHEMA) {
		return -EINVAL;
	}

	if (sizeof(*header) + header->body_size > len) {
		return -EINVAL;
	}

	return copy_onto_defaults(out, sizeof(*out),
				  (const uint8_t *)blob + sizeof(*header),
				  header->body_size, &defaults);
}

size_t tag_stats_encode(const tag_statistics_t *in, void *blob, size_t max)
{
	if (in == NULL) {
		return 0;
	}

	return encode(TAG_STATS_SCHEMA, in, (uint16_t)sizeof(*in), blob, max);
}
