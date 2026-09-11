/*
 * Zigbee sleepy end device built on the Nordic Zigbee R23 add-on (ZBOSS).
 *
 * Compiles to a no-op unless CONFIG_ZIGBEE_ADD_ON is enabled, so the base
 * BLE build does not depend on the add-on being installed.
 */
#ifndef SMART_TAG_ZIGBEE_MANAGER_H
#define SMART_TAG_ZIGBEE_MANAGER_H

#include <stdbool.h>

/**
 * Registers the Smart Tag endpoint, seeds cluster attributes, configures the
 * sleepy end device behaviour and starts the ZBOSS thread. Call after
 * sensor_hub_init() and settings_load().
 */
int zigbee_manager_init(void);

/** True once the tag has joined a Zigbee network. */
bool zigbee_manager_is_joined(void);

/** Erases the ZBOSS persistent storage so the tag re-commissions. */
void zigbee_manager_factory_reset(void);

#endif /* SMART_TAG_ZIGBEE_MANAGER_H */
