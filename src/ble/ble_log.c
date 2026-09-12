#include <zephyr/kernel.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "ble_log.h"
#include "ble_gatt.h"
#include "storage/event_log.h"
#include "storage/sensor_log.h"
#include "power/watchdog.h"

LOG_MODULE_REGISTER(ble_log, LOG_LEVEL_INF);

/*
 * Sized so a chunk fits inside one 247-byte ATT MTU with room for the header.
 * Eight sensor records (13 B) or larger counts of event records (8 B) fit
 * comfortably; the cap is on bytes, not records.
 */
#define CHUNK_PAYLOAD_MAX 180
#define CHUNK_GAP_MS	  10

struct stream_state {
	bool active;
	uint8_t log_id;
	uint16_t chunk_index;
	uint32_t next_seq;
	uint32_t remaining;
};

static struct stream_state stream;
static struct k_work_delayable stream_work;

static uint8_t record_size(uint8_t log_id)
{
	return (log_id == BLE_LOG_ID_SENSOR) ? sizeof(sensor_log_record_t) :
					       sizeof(event_record_t);
}

static int read_records(uint8_t log_id, uint32_t start_seq, void *out,
			size_t max)
{
	if (log_id == BLE_LOG_ID_SENSOR) {
		return sensor_log_read(start_seq, out, max);
	}

	return event_log_read(start_seq, out, max);
}

/*
 * Sequence numbers are contiguous, so the newest record's sequence tells us
 * where a chunk ended without needing the records decoded here.
 */
static void stream_work_fn(struct k_work *work)
{
	uint8_t buffer[sizeof(struct ble_log_chunk_header) + CHUNK_PAYLOAD_MAX];
	struct ble_log_chunk_header *header = (void *)buffer;
	uint8_t *payload = buffer + sizeof(*header);
	uint8_t size = record_size(stream.log_id);
	uint32_t want;
	int got;
	int err;

	ARG_UNUSED(work);

	smart_tag_watchdog_feed();

	if (!stream.active) {
		return;
	}

	if (!ble_gatt_is_subscribed(BLE_CHAR_LOG_DATA)) {
		LOG_WRN("log stream aborted: client unsubscribed");
		stream.active = false;
		return;
	}

	want = MIN(stream.remaining, (uint32_t)(CHUNK_PAYLOAD_MAX / size));

	got = read_records(stream.log_id, stream.next_seq, payload, want);

	header->chunk_index = stream.chunk_index;
	header->log_id = stream.log_id;
	header->record_count = (uint8_t)MAX(got, 0);
	header->record_size = size;
	header->flags = 0;
	header->first_seq = stream.next_seq;

	if (got <= 0 || want == 0U) {
		/* Nothing left: send an empty terminating chunk. */
		header->record_count = 0;
		header->flags = BLE_LOG_CHUNK_FLAG_LAST;
		stream.active = false;

		(void)ble_gatt_notify(BLE_CHAR_LOG_DATA, buffer,
				      sizeof(*header));
		LOG_INF("log stream finished after %u chunks",
			stream.chunk_index);
		return;
	}

	stream.remaining -= MIN(stream.remaining, (uint32_t)got);

	if (stream.remaining == 0U) {
		header->flags = BLE_LOG_CHUNK_FLAG_LAST;
	}

	err = ble_gatt_notify(BLE_CHAR_LOG_DATA, buffer,
			      (uint16_t)(sizeof(*header) + (size_t)got * size));

	if (err == -ENOMEM) {
		/* Out of buffers: keep the position and retry shortly. */
		k_work_reschedule(&stream_work, K_MSEC(CHUNK_GAP_MS * 2));
		return;
	}

	if (err) {
		LOG_WRN("log chunk %u failed: %d", stream.chunk_index, err);
		stream.active = false;
		return;
	}

	stream.chunk_index++;
	stream.next_seq += (uint32_t)got;

	if (stream.remaining == 0U) {
		stream.active = false;
		LOG_INF("log stream finished after %u chunks",
			stream.chunk_index);
		return;
	}

	k_work_reschedule(&stream_work, K_MSEC(CHUNK_GAP_MS));
}

int ble_log_init(void)
{
	k_work_init_delayable(&stream_work, stream_work_fn);
	memset(&stream, 0, sizeof(stream));

	return 0;
}

int ble_log_handle_request(const struct ble_log_request *request)
{
	struct flash_log_info info;

	if (request->opcode == BLE_LOG_OP_ABORT) {
		ble_log_abort();
		return 0;
	}

	if (request->opcode != BLE_LOG_OP_START) {
		return -ENOTSUP;
	}

	if (request->log_id > BLE_LOG_ID_SENSOR) {
		return -EINVAL;
	}

	if (!ble_gatt_is_subscribed(BLE_CHAR_LOG_DATA)) {
		LOG_WRN("log download requested without subscribing to Log Data");
		return -EACCES;
	}

	if (request->log_id == BLE_LOG_ID_SENSOR) {
		sensor_log_get_info(&info);
	} else {
		event_log_get_info(&info);
	}

	stream.active = true;
	stream.log_id = request->log_id;
	stream.chunk_index = 0;
	stream.next_seq = request->start_seq;
	stream.remaining = (request->max_records == 0U) ? info.record_count :
							 request->max_records;

	LOG_INF("log %u download from seq %u, up to %u records",
		stream.log_id, stream.next_seq, stream.remaining);

	k_work_reschedule(&stream_work, K_NO_WAIT);

	return 0;
}

void ble_log_get_info(struct ble_log_info *out)
{
	struct flash_log_info events;
	struct flash_log_info sensors;

	event_log_get_info(&events);
	sensor_log_get_info(&sensors);

	memset(out, 0, sizeof(*out));
	out->event_count = events.record_count;
	out->event_oldest_seq = events.oldest_seq;
	out->event_newest_seq = events.newest_seq;
	out->event_record_size = events.record_size;
	out->sensor_count = sensors.record_count;
	out->sensor_oldest_seq = sensors.oldest_seq;
	out->sensor_newest_seq = sensors.newest_seq;
	out->sensor_record_size = sensors.record_size;
	out->streaming = stream.active ? 1U : 0U;
}

void ble_log_abort(void)
{
	if (stream.active) {
		LOG_INF("log stream aborted");
	}

	stream.active = false;
	(void)k_work_cancel_delayable(&stream_work);
}

ssize_t ble_log_read_info(struct bt_conn *conn,
			  const struct bt_gatt_attr *attr, void *buf,
			  uint16_t len, uint16_t offset)
{
	struct ble_log_info info;

	ble_log_get_info(&info);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &info,
				 sizeof(info));
}

ssize_t ble_log_write_control(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr, const void *buf,
			      uint16_t len, uint16_t offset, uint8_t flags)
{
	struct ble_log_request request;
	int err;

	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len != sizeof(request)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	memcpy(&request, buf, sizeof(request));

	err = ble_log_handle_request(&request);
	if (err == -EINVAL || err == -ENOTSUP) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	if (err) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	return len;
}
