/*
 * Configuration and command characteristics - PLAN.md section 8.
 *
 * Separated from ble_gatt.c so the service table stays a table and the
 * validation and command dispatch live somewhere they can be read.
 */
#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

#include <zephyr/bluetooth/gatt.h>

#include "app/smart_tag.h"

/* Command opcodes, written to the Command characteristic. */
typedef enum {
	BLE_CMD_IDENTIFY = 0x01,
	BLE_CMD_CLEAR_EVENT_LOG = 0x02,
	BLE_CMD_CLEAR_SENSOR_LOG = 0x03,
	BLE_CMD_RESET_COUNTERS = 0x04,
	BLE_CMD_FACTORY_RESET = 0x05,
	BLE_CMD_SAMPLE_NOW = 0x06,
	BLE_CMD_SET_MODE = 0x07,
	BLE_CMD_CLEAR_ALARMS = 0x08,
	BLE_CMD_REBOOT = 0x09,
	BLE_CMD_START_CALIBRATION = 0x0a,
	BLE_CMD_SET_TIME = 0x0b,
	BLE_CMD_SNAPSHOT_GAS_BASELINE = 0x0c,
} ble_cmd_t;

struct ble_cmd_request {
	uint8_t opcode;
	uint8_t reserved;
	uint32_t argument;
} __packed;

struct ble_cmd_response {
	uint8_t opcode;
	int8_t status;		/* 0 = ok, otherwise a negated errno */
	uint16_t payload_len;
	uint8_t payload[32];
} __packed;

int ble_config_init(void);

/* GATT handlers, referenced by the service table in ble_gatt.c. */
ssize_t ble_config_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset);
ssize_t ble_config_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset,
			 uint8_t flags);
ssize_t ble_config_write_command(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr,
				 const void *buf, uint16_t len, uint16_t offset,
				 uint8_t flags);

#endif /* BLE_CONFIG_H */
