#include <zephyr/kernel.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <stddef.h>
#include <string.h>

#include "ble_config.h"
#include "ble_gatt.h"
#include "ble_log.h"
#include "app/app_config.h"
#include "app/app_state.h"
#include "storage/event_log.h"
#include "storage/sensor_log.h"
#include "ui/led_manager.h"
#include "power/power_manager.h"

LOG_MODULE_REGISTER(ble_config, LOG_LEVEL_INF);

#define IDENTIFY_DURATION_S 10

/* Commands run on the workqueue: some erase flash or reboot. */
static struct k_work command_work;
static struct ble_cmd_request pending_command;

static void send_response(uint8_t opcode, int8_t status, const void *payload,
			  uint16_t payload_len)
{
	struct ble_cmd_response response = { 0 };

	if (payload_len > sizeof(response.payload)) {
		payload_len = sizeof(response.payload);
	}

	response.opcode = opcode;
	response.status = status;
	response.payload_len = payload_len;

	if (payload != NULL && payload_len > 0U) {
		memcpy(response.payload, payload, payload_len);
	}

	(void)ble_gatt_notify(BLE_CHAR_COMMAND, &response,
			      (uint16_t)(offsetof(struct ble_cmd_response,
						  payload) +
					 payload_len));
}

static void command_work_fn(struct k_work *work)
{
	struct ble_cmd_request cmd = pending_command;
	tag_statistics_t statistics;
	int err = 0;

	ARG_UNUSED(work);

	switch (cmd.opcode) {
	case BLE_CMD_IDENTIFY:
		led_manager_identify(IDENTIFY_DURATION_S);
		break;

	case BLE_CMD_CLEAR_EVENT_LOG:
		ble_log_abort();
		err = event_log_clear();
		break;

	case BLE_CMD_CLEAR_SENSOR_LOG:
		ble_log_abort();
		err = sensor_log_clear();
		break;

	case BLE_CMD_RESET_COUNTERS:
		app_state_reset_counters();
		break;

	case BLE_CMD_CLEAR_ALARMS:
		app_state_reset_counters();
		break;

	case BLE_CMD_SAMPLE_NOW:
		power_manager_sample_now();
		break;

	case BLE_CMD_SET_MODE:
		err = app_config_set_mode((tag_mode_t)cmd.argument);
		break;

	case BLE_CMD_START_CALIBRATION:
		/*
		 * Not implemented on either board, for slightly different
		 * reasons. The HOLyiot board's SHT40 is factory calibrated and
		 * the Zephyr lis2dh driver does not expose the accelerometer's
		 * offset registers. On the nRF54L15 Tag the parts that would
		 * benefit are the ADXL367, whose offset registers the driver
		 * likewise does not expose, and the BME688's gas sensor, whose
		 * useful "calibration" is a per-unit resistance baseline
		 * captured after burn-in - which belongs in the gateway's
		 * commissioning flow, written down through the Configuration
		 * characteristic's gas_low_threshold_ohm, not in a firmware
		 * command that has nowhere to store its result.
		 *
		 * Reported explicitly rather than silently ignored.
		 */
		err = -ENOTSUP;
		break;

	case BLE_CMD_FACTORY_RESET:
		send_response(cmd.opcode, 0, NULL, 0);
		k_sleep(K_MSEC(200));
		app_state_factory_reset();
		return;

	case BLE_CMD_REBOOT:
		send_response(cmd.opcode, 0, NULL, 0);
		app_state_flush_statistics();
		k_sleep(K_MSEC(200));
		sys_reboot(SYS_REBOOT_WARM);
		return;

	default:
		err = -ENOTSUP;
		break;
	}

	if (cmd.opcode == BLE_CMD_RESET_COUNTERS && err == 0) {
		app_state_get_statistics(&statistics);
		send_response(cmd.opcode, 0, &statistics, sizeof(statistics));
		return;
	}

	send_response(cmd.opcode, (int8_t)err, NULL, 0);
}

int ble_config_init(void)
{
	k_work_init(&command_work, command_work_fn);

	return 0;
}

ssize_t ble_config_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	tag_config_t config;

	app_config_get(&config);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &config,
				 sizeof(config));
}

ssize_t ble_config_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset,
			 uint8_t flags)
{
	tag_config_t config;
	int err;

	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len != sizeof(config)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	memcpy(&config, buf, sizeof(config));

	err = app_config_set(&config);
	if (err == -EINVAL) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	if (err) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	(void)app_raise_event(EVENT_CONFIG_CHANGED, EVENT_SEVERITY_INFO,
			      (int16_t)config.operating_mode);

	return len;
}

ssize_t ble_config_write_command(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr,
				 const void *buf, uint16_t len, uint16_t offset,
				 uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len < 1U || len > sizeof(pending_command)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	memset(&pending_command, 0, sizeof(pending_command));
	memcpy(&pending_command, buf, len);

	k_work_submit(&command_work);

	return len;
}
