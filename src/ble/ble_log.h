/*
 * Chunked log download - PLAN.md section 8.
 *
 * The plan is explicit that the history must not be pushed through a single
 * characteristic. A phone writes a request to Log Control, the tag streams
 * numbered chunks over Log Data, and the last chunk is flagged so the client
 * knows the transfer finished rather than stalled.
 *
 *   Phone                       Tag
 *     |-- LogControl(START) ---->|
 *     |<--- LogData chunk 0 -----|
 *     |<--- LogData chunk 1 -----|
 *     |            ...           |
 *     |<--- LogData chunk N ---->| FLAG_LAST
 */
#ifndef BLE_LOG_H
#define BLE_LOG_H

#include <zephyr/bluetooth/gatt.h>

#include "app/smart_tag.h"

typedef enum {
	BLE_LOG_ID_EVENT = 0,
	BLE_LOG_ID_SENSOR = 1,
} ble_log_id_t;

typedef enum {
	BLE_LOG_OP_START = 1,
	BLE_LOG_OP_ABORT = 2,
} ble_log_op_t;

/** Written to the Log Control characteristic. */
struct ble_log_request {
	uint8_t opcode;		/* ble_log_op_t */
	uint8_t log_id;		/* ble_log_id_t */
	uint32_t start_seq;	/* first sequence number wanted */
	uint16_t max_records;	/* 0 = everything available */
} __packed;

/** Read from the Log Information characteristic. */
struct ble_log_info {
	uint32_t event_count;
	uint32_t event_oldest_seq;
	uint32_t event_newest_seq;
	uint32_t sensor_count;
	uint32_t sensor_oldest_seq;
	uint32_t sensor_newest_seq;
	uint16_t event_record_size;
	uint16_t sensor_record_size;
	uint8_t streaming;
	uint8_t reserved;
} __packed;

/* Chunk header: struct ble_log_chunk_header in app/smart_tag.h (protocol v4). */

int ble_log_init(void);

/** Handles a write to Log Control. Returns 0 or a negative errno. */
int ble_log_handle_request(const struct ble_log_request *request);

void ble_log_get_info(struct ble_log_info *info);

/** Stops any transfer in progress; called on disconnect. */
void ble_log_abort(void);

/* GATT handlers, referenced by the service table in ble_gatt.c. */
ssize_t ble_log_read_info(struct bt_conn *conn,
			  const struct bt_gatt_attr *attr, void *buf,
			  uint16_t len, uint16_t offset);
ssize_t ble_log_write_control(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr, const void *buf,
			      uint16_t len, uint16_t offset, uint8_t flags);

#endif /* BLE_LOG_H */
