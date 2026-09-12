/*
 * Application state - PLAN.md sections 3 and 11.
 *
 * The centre of the firmware. Owns the state machine, holds the live status,
 * and fans each new sensor_data_t out to whichever protocol stacks
 * subscribed. It knows nothing about Zigbee or BLE: they register themselves.
 *
 *   BOOT -> INIT -> ZIGBEE_START -> RUNNING
 *                                     |
 *            sensor timer / motion / BLE request / no activity
 */
#ifndef APP_STATE_H
#define APP_STATE_H

#include <zephyr/kernel.h>

#include "smart_tag.h"

typedef enum {
	APP_STATE_BOOT = 0,
	APP_STATE_INIT,
	APP_STATE_ZIGBEE_START,
	APP_STATE_RUNNING,
	APP_STATE_SERVICE,	/* BLE window open for local maintenance */
	APP_STATE_SLEEP,
} app_state_t;

typedef void (*app_sensor_cb_t)(const sensor_data_t *data, void *user_data);
typedef void (*app_event_cb_t)(const event_record_t *event, void *user_data);

/**
 * Registered by each protocol stack at startup. Callbacks run on the
 * application thread, so they must not block; hand work to a workqueue.
 */
struct app_subscriber {
	sys_snode_t node;
	app_sensor_cb_t on_sensor_data;
	app_event_cb_t on_event;
	void *user_data;
};

int app_state_init(void);

void app_state_subscribe(struct app_subscriber *subscriber);

app_state_t app_state_get(void);

void app_state_set(app_state_t state);

/**
 * The fan-out point from PLAN.md section 3. Updates status, logs history and
 * publishes to every subscriber.
 */
void app_process_sensor_data(sensor_data_t *data);

/** Logs an event and publishes it. Returns the assigned sequence number. */
int app_raise_event(event_type_t type, event_severity_t severity,
		    int16_t value);

void app_state_get_status(tag_status_t *status);

void app_state_get_last_sample(sensor_data_t *data);

void app_state_get_statistics(tag_statistics_t *stats);

/** Persists the running statistics; called periodically and before reboot. */
void app_state_flush_statistics(void);

/** Clears counters and latched alarms, leaving configuration alone. */
void app_state_reset_counters(void);

/** Erases both logs, restores default configuration and reboots. */
void app_state_factory_reset(void);

#endif /* APP_STATE_H */
