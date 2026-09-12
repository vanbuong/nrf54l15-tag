/*
 * Smart Tag GATT service - PLAN.md section 8.
 *
 * PLAN.md lists the characteristics as individual fields (temperature,
 * humidity, pressure, battery, ...). They are grouped here into one packed
 * struct per heading instead, because each extra characteristic is another
 * ATT round trip and radio time is the dominant cost on a coin cell. The
 * groupings match the plan's headings one for one, and the layouts live in
 * app/smart_tag.h so a phone or PC tool can decode them.
 *
 * "Device Information" is served by the standard Bluetooth SIG Device
 * Information Service (0x180A, CONFIG_BT_DIS) rather than a custom
 * characteristic, so generic tools show it without knowing the product.
 *
 *   f0d1a000-9e4b-4b7a-9c2e-2a5b1d250250  Smart Tag Service
 *     f0d1a001  Sensor Data      read, notify   sensor_data_t
 *     f0d1a002  Status           read, notify   tag_status_t
 *     f0d1a003  Configuration    read, write    tag_config_t
 *     f0d1a004  Event            read, notify   event_record_t
 *     f0d1a005  Log Information  read           struct ble_log_info
 *     f0d1a006  Log Control      write          struct ble_log_request
 *     f0d1a007  Log Data         notify         chunked records
 *     f0d1a008  Command          write, notify  request / response
 *     f0d1a009  Statistics       read           tag_statistics_t
 */
#ifndef BLE_GATT_H
#define BLE_GATT_H

#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include "app/smart_tag.h"

#define SMART_TAG_UUID(v)                                                      \
	BT_UUID_128_ENCODE(0xf0d1a000 + (v), 0x9e4b, 0x4b7a, 0x9c2e,           \
			   0x2a5b1d250250)

/* Characteristics addressed by index for notifications. */
typedef enum {
	BLE_CHAR_SENSOR_DATA = 0,
	BLE_CHAR_STATUS,
	BLE_CHAR_EVENT,
	BLE_CHAR_LOG_DATA,
	BLE_CHAR_COMMAND,
	BLE_CHAR_COUNT,
} ble_char_t;

/** Registers the application subscriber. Called by ble_manager_init(). */
int ble_gatt_init(void);

/** Sends a notification on @p which, if the peer has subscribed to it. */
int ble_gatt_notify(ble_char_t which, const void *data, uint16_t len);

bool ble_gatt_is_subscribed(ble_char_t which);

/** Clears all subscription state; called on disconnect. */
void ble_gatt_reset_subscriptions(void);

#endif /* BLE_GATT_H */
