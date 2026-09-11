#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "ble_gatt.h"
#include "ble_config.h"
#include "ble_log.h"
#include "../app/app_state.h"

LOG_MODULE_REGISTER(ble_gatt, LOG_LEVEL_INF);

static const struct bt_uuid_128 uuid_service = BT_UUID_INIT_128(SMART_TAG_UUID(0));
static const struct bt_uuid_128 uuid_sensor = BT_UUID_INIT_128(SMART_TAG_UUID(1));
static const struct bt_uuid_128 uuid_status = BT_UUID_INIT_128(SMART_TAG_UUID(2));
static const struct bt_uuid_128 uuid_config = BT_UUID_INIT_128(SMART_TAG_UUID(3));
static const struct bt_uuid_128 uuid_event = BT_UUID_INIT_128(SMART_TAG_UUID(4));
static const struct bt_uuid_128 uuid_log_info = BT_UUID_INIT_128(SMART_TAG_UUID(5));
static const struct bt_uuid_128 uuid_log_control = BT_UUID_INIT_128(SMART_TAG_UUID(6));
static const struct bt_uuid_128 uuid_log_data = BT_UUID_INIT_128(SMART_TAG_UUID(7));
static const struct bt_uuid_128 uuid_command = BT_UUID_INIT_128(SMART_TAG_UUID(8));
static const struct bt_uuid_128 uuid_statistics = BT_UUID_INIT_128(SMART_TAG_UUID(9));

/*
 * Attribute indices for the notifiable characteristics. Verified against
 * their UUIDs at startup by ble_gatt_init(), because a mis-edit of the table
 * below would otherwise notify the wrong characteristic silently.
 */
static const uint8_t attr_index[BLE_CHAR_COUNT] = {
	[BLE_CHAR_SENSOR_DATA] = 2,
	[BLE_CHAR_STATUS] = 5,
	[BLE_CHAR_EVENT] = 10,
	[BLE_CHAR_LOG_DATA] = 17,
	[BLE_CHAR_COMMAND] = 20,
};

static const struct bt_uuid *attr_uuid[BLE_CHAR_COUNT] = {
	[BLE_CHAR_SENSOR_DATA] = &uuid_sensor.uuid,
	[BLE_CHAR_STATUS] = &uuid_status.uuid,
	[BLE_CHAR_EVENT] = &uuid_event.uuid,
	[BLE_CHAR_LOG_DATA] = &uuid_log_data.uuid,
	[BLE_CHAR_COMMAND] = &uuid_command.uuid,
};

static bool subscribed[BLE_CHAR_COUNT];

/* Last event seen, so a client can read it without subscribing first. */
static event_record_t latest_event;

/* Notifications are queued from the application thread onto the workqueue. */
static struct k_work notify_work;
static struct k_spinlock publish_lock;
static sensor_data_t pending_sensor;
static bool pending_sensor_valid;

K_MSGQ_DEFINE(event_queue, sizeof(event_record_t), 8, 4);

/* --------------------------------------------------------------------------
 * Read handlers
 * ------------------------------------------------------------------------ */

static ssize_t read_sensor(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	sensor_data_t data;

	app_state_get_last_sample(&data);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &data,
				 sizeof(data));
}

static ssize_t read_status(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	tag_status_t status;

	app_state_get_status(&status);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &status,
				 sizeof(status));
}

static ssize_t read_event(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &latest_event,
				 sizeof(latest_event));
}

static ssize_t read_statistics(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset)
{
	tag_statistics_t statistics;

	app_state_get_statistics(&statistics);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &statistics,
				 sizeof(statistics));
}

/* --------------------------------------------------------------------------
 * CCC handlers
 * ------------------------------------------------------------------------ */

static void sensor_ccc(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	subscribed[BLE_CHAR_SENSOR_DATA] = (value == BT_GATT_CCC_NOTIFY);
}

static void status_ccc(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	subscribed[BLE_CHAR_STATUS] = (value == BT_GATT_CCC_NOTIFY);
}

static void event_ccc(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	subscribed[BLE_CHAR_EVENT] = (value == BT_GATT_CCC_NOTIFY);
}

static void log_data_ccc(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	subscribed[BLE_CHAR_LOG_DATA] = (value == BT_GATT_CCC_NOTIFY);
}

static void command_ccc(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	subscribed[BLE_CHAR_COMMAND] = (value == BT_GATT_CCC_NOTIFY);
}

/* --------------------------------------------------------------------------
 * Service table
 * ------------------------------------------------------------------------ */

BT_GATT_SERVICE_DEFINE(
	smart_tag_svc,
	BT_GATT_PRIMARY_SERVICE(&uuid_service),				/* 0 */

	BT_GATT_CHARACTERISTIC(&uuid_sensor.uuid,			/* 1, 2 */
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_sensor, NULL, NULL),
	BT_GATT_CCC(sensor_ccc, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),/* 3 */

	BT_GATT_CHARACTERISTIC(&uuid_status.uuid,			/* 4, 5 */
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_status, NULL, NULL),
	BT_GATT_CCC(status_ccc, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),/* 6 */

	BT_GATT_CHARACTERISTIC(&uuid_config.uuid,			/* 7, 8 */
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       ble_config_read, ble_config_write, NULL),

	BT_GATT_CHARACTERISTIC(&uuid_event.uuid,			/* 9, 10 */
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_event, NULL, NULL),
	BT_GATT_CCC(event_ccc, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),	/* 11 */

	BT_GATT_CHARACTERISTIC(&uuid_log_info.uuid,			/* 12, 13 */
			       BT_GATT_CHRC_READ, BT_GATT_PERM_READ,
			       ble_log_read_info, NULL, NULL),

	BT_GATT_CHARACTERISTIC(&uuid_log_control.uuid,			/* 14, 15 */
			       BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE, NULL,
			       ble_log_write_control, NULL),

	BT_GATT_CHARACTERISTIC(&uuid_log_data.uuid,			/* 16, 17 */
			       BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, NULL,
			       NULL, NULL),
	BT_GATT_CCC(log_data_ccc, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),/* 18 */

	BT_GATT_CHARACTERISTIC(&uuid_command.uuid,			/* 19, 20 */
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_WRITE, NULL,
			       ble_config_write_command, NULL),
	BT_GATT_CCC(command_ccc, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),/* 21 */

	BT_GATT_CHARACTERISTIC(&uuid_statistics.uuid,			/* 22, 23 */
			       BT_GATT_CHRC_READ, BT_GATT_PERM_READ,
			       read_statistics, NULL, NULL),
);

/* --------------------------------------------------------------------------
 * Notification plumbing
 * ------------------------------------------------------------------------ */

bool ble_gatt_is_subscribed(ble_char_t which)
{
	return (which < BLE_CHAR_COUNT) && subscribed[which];
}

int ble_gatt_notify(ble_char_t which, const void *data, uint16_t len)
{
	if (which >= BLE_CHAR_COUNT) {
		return -EINVAL;
	}

	if (!subscribed[which]) {
		return -EACCES;
	}

	return bt_gatt_notify(NULL, &smart_tag_svc.attrs[attr_index[which]],
			      data, len);
}

void ble_gatt_reset_subscriptions(void)
{
	memset(subscribed, 0, sizeof(subscribed));
}

static void notify_work_fn(struct k_work *work)
{
	sensor_data_t sensor;
	event_record_t event;
	tag_status_t status;
	k_spinlock_key_t key;
	bool have_sensor;

	ARG_UNUSED(work);

	key = k_spin_lock(&publish_lock);
	have_sensor = pending_sensor_valid;
	sensor = pending_sensor;
	pending_sensor_valid = false;
	k_spin_unlock(&publish_lock, key);

	if (have_sensor) {
		(void)ble_gatt_notify(BLE_CHAR_SENSOR_DATA, &sensor,
				      sizeof(sensor));

		app_state_get_status(&status);
		(void)ble_gatt_notify(BLE_CHAR_STATUS, &status, sizeof(status));
	}

	while (k_msgq_get(&event_queue, &event, K_NO_WAIT) == 0) {
		if (ble_gatt_notify(BLE_CHAR_EVENT, &event, sizeof(event)) ==
		    -ENOMEM) {
			/* Out of buffers: retry this one on the next pass. */
			(void)k_msgq_put(&event_queue, &event, K_NO_WAIT);
			k_work_submit(&notify_work);
			return;
		}
	}
}

static void on_sensor_data(const sensor_data_t *data, void *user_data)
{
	k_spinlock_key_t key;

	ARG_UNUSED(user_data);

	key = k_spin_lock(&publish_lock);
	pending_sensor = *data;
	pending_sensor_valid = true;
	k_spin_unlock(&publish_lock, key);

	k_work_submit(&notify_work);
}

static void on_event(const event_record_t *event, void *user_data)
{
	ARG_UNUSED(user_data);

	latest_event = *event;

	if (k_msgq_put(&event_queue, event, K_NO_WAIT) != 0) {
		event_record_t dropped;

		/* Drop the oldest so the newest still reaches the client. */
		(void)k_msgq_get(&event_queue, &dropped, K_NO_WAIT);
		(void)k_msgq_put(&event_queue, event, K_NO_WAIT);
	}

	k_work_submit(&notify_work);
}

static struct app_subscriber ble_subscriber = {
	.on_sensor_data = on_sensor_data,
	.on_event = on_event,
};

int ble_gatt_init(void)
{
	/* Catch a service table edit that moved a value attribute. */
	for (int i = 0; i < BLE_CHAR_COUNT; i++) {
		if (bt_uuid_cmp(smart_tag_svc.attrs[attr_index[i]].uuid,
				attr_uuid[i]) != 0) {
			LOG_ERR("attribute index %u does not match its UUID - "
				"notifications would target the wrong characteristic",
				i);
			return -EFAULT;
		}
	}

	k_work_init(&notify_work, notify_work_fn);
	app_state_subscribe(&ble_subscriber);

	return 0;
}
