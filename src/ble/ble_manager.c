/*
 * BLE manager - PLAN.md sections 7 and 11.
 *
 * BLE is the local maintenance channel. It is off in normal operation; the
 * power manager opens a window on a button press, and the window closes on a
 * timeout. That is what keeps advertising off the battery budget.
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

#include "ble_manager.h"
#include "ble_gatt.h"
#include "ble_config.h"
#include "ble_log.h"
#include "../app/app_state.h"
#include "../app/app_config.h"
#include "../ui/led_manager.h"

LOG_MODULE_REGISTER(ble_manager, LOG_LEVEL_INF);

/*
 * 0xFFFF is the "no company assigned" identifier. Replace with the product's
 * allocated Bluetooth SIG company ID before release.
 */
#define COMPANY_ID 0xffff

static struct bt_conn *current_conn;
static bool advertising;
static bool stack_ready;

/*
 * Scan-response manufacturer data: enough for a phone to triage a shelf of
 * tags without connecting to each one.
 */
static uint8_t manuf_data[] = {
	COMPANY_ID & 0xff,
	COMPANY_ID >> 8,
	SMART_TAG_PROTOCOL_VERSION,
	0,	/* operating mode */
	0,	/* motion state << 4 | orientation */
	0, 0,	/* alarm flags */
	0, 0,	/* temperature, 0.01 C */
	0, 0,	/* humidity, 0.01 %RH */
	0,	/* battery percent */
};

static const struct bt_data adv_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, SMART_TAG_UUID(0)),
};

static struct bt_data scan_response[] = {
	BT_DATA(BT_DATA_MANUFACTURER_DATA, manuf_data, sizeof(manuf_data)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

void ble_manager_update_adv_data(void)
{
	sensor_data_t sample;
	tag_status_t status;

	app_state_get_last_sample(&sample);
	app_state_get_status(&status);

	manuf_data[3] = status.operating_mode;
	manuf_data[4] = (uint8_t)((status.motion_state << 4) |
				  (status.orientation & 0x0f));
	sys_put_le16(status.alarm_flags, &manuf_data[5]);
	sys_put_le16((uint16_t)sample.temperature_c_x100, &manuf_data[7]);
	sys_put_le16(sample.humidity_x100, &manuf_data[9]);
	manuf_data[11] = sample.battery_percent;

	if (advertising) {
		(void)bt_le_adv_update_data(adv_data, ARRAY_SIZE(adv_data),
					    scan_response,
					    ARRAY_SIZE(scan_response));
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		LOG_ERR("connection failed: %u", err);
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("connected: %s", addr);

	current_conn = bt_conn_ref(conn);

	/* A live connection replaces the advertising indication. */
	led_manager_set(LED_STATE_BLE_ADVERTISING, false);

	(void)app_raise_event(EVENT_BLE_CONNECTED, EVENT_SEVERITY_INFO, 0);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	LOG_INF("disconnected: 0x%02x", reason);

	ble_gatt_reset_subscriptions();
	ble_log_abort();

	if (current_conn != NULL) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}

	/*
	 * The controller stops advertising on connect. Restart it if the
	 * service window is still open; the power manager closes the window
	 * by calling ble_manager_stop_advertising().
	 */
	if (advertising) {
		advertising = false;
		(void)ble_manager_start_advertising();
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void set_device_name(void)
{
	char name[CONFIG_BT_DEVICE_NAME_MAX];
	bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
	size_t count = ARRAY_SIZE(addrs);

	bt_id_get(addrs, &count);

	if (count == 0U) {
		return;
	}

	/* Last two address bytes make a tag identifiable on a shelf. */
	(void)snprintk(name, sizeof(name), "%s-%02X%02X", CONFIG_BT_DEVICE_NAME,
		       addrs[0].a.val[1], addrs[0].a.val[0]);

	if (bt_set_name(name) == 0) {
		scan_response[1].data = (const uint8_t *)bt_get_name();
		scan_response[1].data_len = (uint8_t)strlen(bt_get_name());
	}
}

int ble_manager_init(void)
{
	int err;

	err = ble_gatt_init();
	if (err) {
		return err;
	}

	err = ble_config_init();
	if (err) {
		return err;
	}

	err = ble_log_init();
	if (err) {
		return err;
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bluetooth init failed: %d", err);
		return err;
	}

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		/* Bonding keys; the application already ran settings_load(). */
		(void)settings_load_subtree("bt");
	}

	set_device_name();
	stack_ready = true;

	LOG_INF("bluetooth ready as \"%s\" (advertising on demand)",
		bt_get_name());

	return 0;
}

int ble_manager_start_advertising(void)
{
	int err;

	if (!stack_ready) {
		return -EAGAIN;
	}

	if (advertising) {
		return 0;
	}

	ble_manager_update_adv_data();

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, adv_data,
			      ARRAY_SIZE(adv_data), scan_response,
			      ARRAY_SIZE(scan_response));
	if (err) {
		LOG_ERR("advertising failed to start: %d", err);
		return err;
	}

	advertising = true;
	led_manager_set(LED_STATE_BLE_ADVERTISING, true);
	LOG_INF("advertising started");

	return 0;
}

void ble_manager_stop_advertising(void)
{
	if (!advertising) {
		return;
	}

	advertising = false;

	if (current_conn != NULL) {
		(void)bt_conn_disconnect(current_conn,
					 BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}

	(void)bt_le_adv_stop();
	led_manager_set(LED_STATE_BLE_ADVERTISING, false);

	LOG_INF("advertising stopped");
}

bool ble_manager_is_advertising(void)
{
	return advertising;
}

bool ble_manager_is_connected(void)
{
	return current_conn != NULL;
}
