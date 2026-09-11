/*
 * Runtime configuration - PLAN.md section 3.
 *
 * Holds the one authoritative copy of tag_config_t, validates changes coming
 * from BLE or Zigbee, and pushes accepted changes to storage and to the
 * modules that cache thresholds.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "smart_tag.h"

/** Loads the stored configuration. Call after settings_load(). */
int app_config_init(void);

void app_config_get(tag_config_t *config);

/**
 * Validates, applies and persists a configuration. Returns -EINVAL and
 * changes nothing if any field is out of range.
 */
int app_config_set(const tag_config_t *config);

/** Convenience for the single-field writes Zigbee attribute writes produce. */
int app_config_set_mode(tag_mode_t mode);
int app_config_set_sampling_interval(uint16_t seconds);

/** True if @p config would be accepted by app_config_set(). */
bool app_config_validate(const tag_config_t *config);

/** Restores defaults and persists them. */
int app_config_reset(void);

/**
 * Effective sampling interval for the current operating mode. Environment
 * tags sample slowly; shipping tags sample often enough to catch impacts.
 */
uint16_t app_config_effective_interval_s(void);

#endif /* APP_CONFIG_H */
