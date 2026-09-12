#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "config_storage.h"
#include "config_migrate.h"

LOG_MODULE_REGISTER(config_storage, LOG_LEVEL_INF);

#define SETTINGS_ROOT		CONFIG_STORAGE_SETTINGS_ROOT
#define SETTINGS_KEY_CONFIG	SETTINGS_ROOT "/config"
#define SETTINGS_KEY_STATS	SETTINGS_ROOT "/stats"

#define SETTINGS_BLOB_MAX	64U

static tag_config_t stored_config = TAG_CONFIG_DEFAULTS;
static tag_statistics_t stored_stats;

/*
 * Accept protocol-v3 raw structs (exact v0 size) and schema-1 framed
 * blobs. Anything else is safer as defaults than as a reinterpreted
 * layout from a future or corrupt write.
 */
static int settings_set(const char *name, size_t len, settings_read_cb read_cb,
			void *cb_arg)
{
	uint8_t blob[SETTINGS_BLOB_MAX];
	const char *next;

	if (len == 0U || len > sizeof(blob)) {
		LOG_WRN("stored %s is %u B - keeping defaults", name,
			(unsigned int)len);
		return 0;
	}

	if (read_cb(cb_arg, blob, len) != (ssize_t)len) {
		return -EIO;
	}

	if (settings_name_steq(name, "config", &next) && !next) {
		if (tag_config_decode(blob, len, &stored_config) != 0) {
			stored_config = (tag_config_t)TAG_CONFIG_DEFAULTS;
			LOG_WRN("stored config rejected - keeping defaults");
			return 0;
		}

		LOG_INF("configuration restored (schema %u, %u B)",
			(len == TAG_CONFIG_V0_SIZE) ? 0U : TAG_CONFIG_SCHEMA,
			(unsigned int)len);
		return 0;
	}

	if (settings_name_steq(name, "stats", &next) && !next) {
		if (tag_stats_decode(blob, len, &stored_stats) != 0) {
			memset(&stored_stats, 0, sizeof(stored_stats));
			LOG_WRN("stored statistics rejected - resetting");
			return 0;
		}

		LOG_INF("statistics restored (schema %u, %u B)",
			(len == TAG_STATS_V0_SIZE) ? 0U : TAG_STATS_SCHEMA,
			(unsigned int)len);
		return 0;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(smart_tag_storage, SETTINGS_ROOT, NULL,
			       settings_set, NULL, NULL);

int config_storage_init(void)
{
	/* The handler is registered statically; nothing to do at runtime. */
	return 0;
}

void config_storage_load(tag_config_t *config)
{
	*config = stored_config;
}

int config_storage_save(const tag_config_t *config)
{
	uint8_t blob[SETTINGS_BLOB_MAX];
	size_t n;
	int err;

	stored_config = *config;

	n = tag_config_encode(config, blob, sizeof(blob));
	if (n == 0U) {
		return -ENOMEM;
	}

	err = settings_save_one(SETTINGS_KEY_CONFIG, blob, n);
	if (err) {
		LOG_ERR("cannot persist configuration: %d", err);
	}

	return err;
}

void config_storage_load_statistics(tag_statistics_t *stats)
{
	*stats = stored_stats;
}

int config_storage_save_statistics(const tag_statistics_t *stats)
{
	uint8_t blob[SETTINGS_BLOB_MAX];
	size_t n;
	int err;

	stored_stats = *stats;

	n = tag_stats_encode(stats, blob, sizeof(blob));
	if (n == 0U) {
		return -ENOMEM;
	}

	err = settings_save_one(SETTINGS_KEY_STATS, blob, n);
	if (err) {
		LOG_ERR("cannot persist statistics: %d", err);
	}

	return err;
}

int config_storage_factory_reset(void)
{
	tag_config_t defaults = TAG_CONFIG_DEFAULTS;
	int err;

	memset(&stored_stats, 0, sizeof(stored_stats));
	stored_config = defaults;

	err = settings_delete(SETTINGS_KEY_CONFIG);
	if (err) {
		LOG_WRN("cannot delete stored config: %d", err);
	}

	err = settings_delete(SETTINGS_KEY_STATS);
	if (err) {
		LOG_WRN("cannot delete stored statistics: %d", err);
	}

	return 0;
}
