/*
 * Configuration and statistics persistence - PLAN.md section 9.
 *
 * PLAN.md places configuration in the external NOR alongside the logs. This
 * implementation keeps it in the internal RRAM storage partition through
 * Zephyr's settings subsystem instead, for two reasons: the Bluetooth stack
 * already requires settings on internal flash for its bonding keys, and
 * keeping the small read-modify-write config away from the log partitions
 * avoids erasing a 4 KB NOR sector to change one threshold. The external NOR
 * carries the two logs, which is where its capacity is actually needed.
 */
#ifndef CONFIG_STORAGE_H
#define CONFIG_STORAGE_H

#include "../app/smart_tag.h"

/*
 * Shared with app_main.c: settings_load() there must load only this
 * subtree, not the whole settings tree. A blanket settings_load() also
 * restores Bluetooth's statically-registered handlers (e.g. bt/sc, the
 * GATT service-changed state), and it runs before ble_manager_init() has
 * called bt_enable() - so it was scheduling gatt_sc's delayed work item
 * before bt_gatt_init() had ever initialized it, crashing the system
 * workqueue with a NULL work handler once that work item ran. BLE's own
 * settings load correctly happens later, in ble_manager_init(), after
 * bt_enable().
 */
#define CONFIG_STORAGE_SETTINGS_ROOT "smart_tag"

/** Registers the settings handlers. Call before settings_load_subtree(). */
int config_storage_init(void);

/** Fills @p config with the stored values, or the defaults if none exist. */
void config_storage_load(tag_config_t *config);

int config_storage_save(const tag_config_t *config);

void config_storage_load_statistics(tag_statistics_t *stats);

int config_storage_save_statistics(const tag_statistics_t *stats);

/** Discards stored configuration and statistics. */
int config_storage_factory_reset(void);

#endif /* CONFIG_STORAGE_H */
