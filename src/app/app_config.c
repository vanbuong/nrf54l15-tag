#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_config.h"
#include "storage/config_storage.h"
#include "motion/motion_manager.h"
#include "power/power_manager.h"
#include "ui/led_manager.h"

LOG_MODULE_REGISTER(app_config, LOG_LEVEL_INF);

static struct k_mutex lock;
static tag_config_t config = TAG_CONFIG_DEFAULTS;

/*
 * Operating-mode multipliers applied to the configured sampling interval.
 * A shipping tag has to sample often enough to catch an impact between
 * wake-ups; an environment tag on a shelf does not.
 */
static uint16_t mode_interval_divider(tag_mode_t mode)
{
	switch (mode) {
	case TAG_MODE_SHIPPING:
		return 4;	/* four times as often */
	case TAG_MODE_ASSET:
		return 2;
	case TAG_MODE_ENVIRONMENT:
	default:
		return 1;
	}
}

bool app_config_validate(const tag_config_t *cfg)
{
	if (cfg->sampling_interval_s < 1 || cfg->sampling_interval_s > 3600) {
		return false;
	}

	if (cfg->motion_timeout_s < 1 || cfg->motion_timeout_s > 3600) {
		return false;
	}

	if (cfg->motion_threshold_mg < 20 || cfg->motion_threshold_mg > 4000) {
		return false;
	}

	if (cfg->shock_threshold_mg < 500 || cfg->shock_threshold_mg > 16000) {
		return false;
	}

	if (cfg->free_fall_threshold_mg > 900) {
		return false;
	}

	if (cfg->temp_high_c_x100 <= cfg->temp_low_c_x100) {
		return false;
	}

	if (cfg->humidity_high_x100 > 10000) {
		return false;
	}

	if (cfg->battery_low_mv < 1800 || cfg->battery_low_mv > 3300) {
		return false;
	}

	if (cfg->report_max_interval_s < 60 ||
	    cfg->report_max_interval_s > 43200) {
		return false;
	}

	if (cfg->ble_service_window_s < 10 ||
	    cfg->ble_service_window_s > 3600) {
		return false;
	}

	if (cfg->operating_mode > TAG_MODE_SHIPPING) {
		return false;
	}

	if (cfg->led_enabled > 1U || cfg->sensor_history_enabled > 1U) {
		return false;
	}

	return true;
}

int app_config_init(void)
{
	tag_config_t stored;

	k_mutex_init(&lock);

	config_storage_load(&stored);

	if (!app_config_validate(&stored)) {
		tag_config_t defaults = TAG_CONFIG_DEFAULTS;

		LOG_WRN("stored configuration is out of range - using defaults");
		stored = defaults;
	}

	config = stored;

	LOG_INF("mode %u, sampling %u s (effective %u s)",
		config.operating_mode, config.sampling_interval_s,
		app_config_effective_interval_s());

	return 0;
}

void app_config_get(tag_config_t *out)
{
	k_mutex_lock(&lock, K_FOREVER);
	*out = config;
	k_mutex_unlock(&lock);
}

int app_config_set(const tag_config_t *in)
{
	tag_config_t applied;

	if (!app_config_validate(in)) {
		LOG_WRN("rejected out-of-range configuration");
		return -EINVAL;
	}

	k_mutex_lock(&lock, K_FOREVER);
	config = *in;
	applied = config;
	k_mutex_unlock(&lock);

	motion_manager_update_config(&applied);
	led_manager_set_enabled(applied.led_enabled != 0U);
	power_manager_config_changed();

	return config_storage_save(&applied);
}

int app_config_set_mode(tag_mode_t mode)
{
	tag_config_t updated;

	app_config_get(&updated);
	updated.operating_mode = (uint8_t)mode;

	return app_config_set(&updated);
}

int app_config_set_sampling_interval(uint16_t seconds)
{
	tag_config_t updated;

	app_config_get(&updated);
	updated.sampling_interval_s = seconds;

	return app_config_set(&updated);
}

int app_config_reset(void)
{
	tag_config_t defaults = TAG_CONFIG_DEFAULTS;

	return app_config_set(&defaults);
}

uint16_t app_config_effective_interval_s(void)
{
	uint16_t interval;
	uint16_t divider;

	k_mutex_lock(&lock, K_FOREVER);
	interval = config.sampling_interval_s;
	divider = mode_interval_divider((tag_mode_t)config.operating_mode);
	k_mutex_unlock(&lock);

	interval /= divider;

	return MAX(interval, 1);
}
