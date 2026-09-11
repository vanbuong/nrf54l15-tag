/*
 * BLE manager - PLAN.md sections 7 and 11.
 *
 * Owns the stack and the advertising lifecycle. BLE is the local maintenance
 * channel, not a second telemetry protocol: it is off in normal operation and
 * comes up only when the power manager opens a service window.
 */
#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

/** Enables the stack and registers the GATT service. Does not advertise. */
int ble_manager_init(void);

int ble_manager_start_advertising(void);

void ble_manager_stop_advertising(void);

bool ble_manager_is_advertising(void);

bool ble_manager_is_connected(void);

/** Refreshes the status summary carried in the scan response. */
void ble_manager_update_adv_data(void);

#endif /* BLE_MANAGER_H */
