#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "config_storage.h"

LOG_MODULE_REGISTER(config_storage, LOG_LEVEL_INF);

#define SETTINGS_ROOT		CONFIG_STORAGE_SETTINGS_ROOT
#define SETTINGS_KEY_CONFIG	SETTINGS_ROOT "/config"
#define SETTINGS_KEY_STATS	SETTINGS_ROOT "/stats"

static tag_config_t stored_config = TAG_CONFIG_DEFAULTS;
static tag_statistics_t stored_stats;

/*
 * A stored blob is only accepted if it is exactly the size this firmware
 * expects. A shorter or longer record means the struct changed shape across
 * a firmware update, in which case the defaults are safer than a
 * reinterpreted blob.
 */
static int settings_set(const char *name, size_t len, settings_read_cb read_cb,
			void *cb_arg)
{
	const char *next;

	if (settings_name_steq(name, "config", &next) && !next) {
		if (len != sizeof(stored_config)) {
			LOG_WRN("stored config is %u B, expected %u - keeping defaults",
				(unsigned int)len,
				(unsigned int)sizeof(stored_config));
			return 0;
		}

		if (read_cb(cb_arg, &stored_config, sizeof(stored_config)) !=
		    (ssize_t)sizeof(stored_config)) {
			return -EIO;
		}

		LOG_INF("configuration restored");
		return 0;
	}

	if (settings_name_steq(name, "stats", &next) && !next) {
		if (len != sizeof(stored_stats)) {
			LOG_WRN("stored statistics are %u B, expected %u - resetting",
				(unsigned int)len,
				(unsigned int)sizeof(stored_stats));
			return 0;
		}

		if (read_cb(cb_arg, &stored_stats, sizeof(stored_stats)) !=
		    (ssize_t)sizeof(stored_stats)) {
			return -EIO;
		}

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
	int err;

	stored_config = *config;

	err = settings_save_one(SETTINGS_KEY_CONFIG, config, sizeof(*config));
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
	int err;

	stored_stats = *stats;

	err = settings_save_one(SETTINGS_KEY_STATS, stats, sizeof(*stats));
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
