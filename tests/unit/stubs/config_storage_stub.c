#include <string.h>

#include "storage/config_storage.h"

static tag_config_t stored_config = TAG_CONFIG_DEFAULTS;
static tag_statistics_t stored_stats;

int config_storage_init(void)
{
	return 0;
}

void config_storage_load(tag_config_t *config)
{
	*config = stored_config;
}

int config_storage_save(const tag_config_t *config)
{
	stored_config = *config;
	return 0;
}

void config_storage_load_statistics(tag_statistics_t *stats)
{
	*stats = stored_stats;
}

int config_storage_save_statistics(const tag_statistics_t *stats)
{
	stored_stats = *stats;
	return 0;
}

int config_storage_factory_reset(void)
{
	stored_config = (tag_config_t)TAG_CONFIG_DEFAULTS;
	memset(&stored_stats, 0, sizeof(stored_stats));
	return 0;
}

void config_storage_stub_reset(void)
{
	stored_config = (tag_config_t)TAG_CONFIG_DEFAULTS;
	memset(&stored_stats, 0, sizeof(stored_stats));
}
