#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <errno.h>
#include <string.h>

#include "app_state.h"
#include "app_config.h"
#include "motion/motion_manager.h"
#include "sensors/sensor_manager.h"
#include "storage/event_log.h"
#include "storage/sensor_log.h"
#include "storage/config_storage.h"
#include "ui/led_manager.h"
#include "app_alarms.h"

LOG_MODULE_REGISTER(app_state, LOG_LEVEL_INF);

/* Statistics are written this often, to bound NVS wear. */
#define STATS_FLUSH_INTERVAL_S 3600

static struct k_mutex lock;
static sys_slist_t subscribers;

static app_state_t state = APP_STATE_BOOT;
static tag_status_t status;
static tag_statistics_t statistics;
static sensor_data_t last_sample;
static uint32_t last_stats_flush;

/*
 * Events detected while holding the lock are queued here and raised after it
 * is dropped, because raising an event takes the lock and calls out to
 * subscribers.
 */
#define MAX_PENDING_EVENTS 8

struct pending_event {
	uint8_t type;
	uint8_t severity;
	int16_t value;
};

static struct pending_event pending[MAX_PENDING_EVENTS];
static uint8_t pending_count;

/* Caller holds the lock. */
static void queue_event(event_type_t type, event_severity_t severity,
			int16_t value)
{
	if (pending_count >= MAX_PENDING_EVENTS) {
		return;
	}

	pending[pending_count].type = (uint8_t)type;
	pending[pending_count].severity = (uint8_t)severity;
	pending[pending_count].value = value;
	pending_count++;
}

int app_state_init(void)
{
	k_mutex_init(&lock);
	sys_slist_init(&subscribers);

	config_storage_load_statistics(&statistics);
	statistics.boot_count++;

	status.motion_state = MOTION_STATE_STATIONARY;
	status.orientation = ORIENTATION_UNKNOWN;
	status.shock_count = statistics.shock_count;
	status.free_fall_count = statistics.free_fall_count;
	status.event_count = statistics.event_count;
	status.max_shock_mg = statistics.max_shock_mg;
	status.movement_duration_s = statistics.movement_duration_s;

	LOG_INF("boot %u, lifetime uptime %u s", statistics.boot_count,
		statistics.total_uptime_s);

	return 0;
}

void app_state_subscribe(struct app_subscriber *subscriber)
{
	if (subscriber == NULL) {
		return;
	}

	/*
	 * Protocol stacks register during INIT / ZIGBEE_START. After
	 * RUNNING the subscriber list is frozen so a late BLE reconnect
	 * cannot duplicate notifications.
	 */
	if (state >= APP_STATE_RUNNING) {
		LOG_WRN("subscriber refused after RUNNING");
		return;
	}

	k_mutex_lock(&lock, K_FOREVER);
	sys_slist_append(&subscribers, &subscriber->node);
	k_mutex_unlock(&lock);
}

app_state_t app_state_get(void)
{
	return state;
}

void app_state_set(app_state_t new_state)
{
	if (state != new_state) {
		LOG_DBG("state %u -> %u", state, new_state);
		state = new_state;
	}
}

int app_raise_event(event_type_t type, event_severity_t severity,
		    int16_t value)
{
	struct app_subscriber *sub;
	event_record_t record;
	uint32_t seq = 0;
	int err;

	err = event_log_add(type, severity, value, &seq);
	if (err) {
		LOG_WRN("event %u not logged: %d", type, err);
	}

	record.timestamp = (uint32_t)(k_uptime_get() / 1000);
	record.type = (uint8_t)type;
	record.severity = (uint8_t)severity;
	record.value = value;

	k_mutex_lock(&lock, K_FOREVER);
	status.event_count++;
	statistics.event_count = status.event_count;
	k_mutex_unlock(&lock);

	/* An alarm-severity event lights the LED until something clears it. */
	if (severity == EVENT_SEVERITY_ALARM) {
		led_manager_set(LED_STATE_ALARM, true);
	}

	SYS_SLIST_FOR_EACH_CONTAINER(&subscribers, sub, node) {
		if (sub->on_event != NULL) {
			sub->on_event(&record, sub->user_data);
		}
	}

	return err;
}

static void drain_pending_events(void)
{
	struct pending_event batch[MAX_PENDING_EVENTS];
	uint8_t count;

	k_mutex_lock(&lock, K_FOREVER);
	count = pending_count;
	memcpy(batch, pending, sizeof(batch[0]) * count);
	pending_count = 0;
	k_mutex_unlock(&lock);

	for (uint8_t i = 0; i < count; i++) {
		app_raise_event((event_type_t)batch[i].type,
				(event_severity_t)batch[i].severity,
				batch[i].value);
	}
}

/* Caller holds the lock. Latching so a transient excursion is still reported. */
static void check_thresholds(const sensor_data_t *data,
			     const tag_config_t *config)
{
	struct app_alarm_event events[APP_ALARM_MAX_EVENTS];
	uint8_t n = 0;
	uint16_t before = status.alarm_flags;
	uint8_t expected = sensor_manager_expected_valid();

	app_alarms_eval(data, config, expected, &status.alarm_flags, events, &n);

	if ((status.alarm_flags & ALARM_SENSOR_FAULT) != 0U &&
	    (before & ALARM_SENSOR_FAULT) == 0U) {
		statistics.sensor_faults++;
	}

	if ((data->valid & SENSOR_VALID_BATTERY) != 0U) {
		if (statistics.min_battery_mv == 0U ||
		    data->battery_mv < statistics.min_battery_mv) {
			statistics.min_battery_mv = data->battery_mv;
		}
	}

	for (uint8_t i = 0; i < n; i++) {
		queue_event((event_type_t)events[i].type,
			    (event_severity_t)events[i].severity,
			    events[i].value);
	}
}

/* Caller holds the lock. Turns motion manager output into queued events. */
static void apply_motion_result(const struct motion_result *motion)
{
	if (motion->events & MOTION_EVENT_START) {
		statistics.motion_count++;
		status.last_movement_time = last_sample.timestamp;
		queue_event(EVENT_MOTION_START, EVENT_SEVERITY_INFO, 0);
	}

	if (motion->events & MOTION_EVENT_STOP) {
		status.movement_duration_s += motion->movement_duration_s;
		statistics.movement_duration_s = status.movement_duration_s;
		queue_event(EVENT_MOTION_STOP, EVENT_SEVERITY_INFO,
			    (int16_t)MIN(motion->movement_duration_s,
					 INT16_MAX));
	}

	if (motion->events & MOTION_EVENT_SHOCK) {
		status.shock_count++;
		statistics.shock_count = status.shock_count;
		status.alarm_flags |= ALARM_SHOCK;

		if (motion->shock_mg > status.max_shock_mg) {
			status.max_shock_mg = motion->shock_mg;
			statistics.max_shock_mg = motion->shock_mg;
		}

		queue_event(EVENT_SHOCK, EVENT_SEVERITY_ALARM,
			    (int16_t)motion->shock_mg);
	}

	if (motion->events & MOTION_EVENT_FREE_FALL) {
		status.free_fall_count++;
		statistics.free_fall_count = status.free_fall_count;
		status.alarm_flags |= ALARM_FREE_FALL;
		queue_event(EVENT_FREE_FALL, EVENT_SEVERITY_ALARM, 0);
	}

	if (motion->events & MOTION_EVENT_ORIENTATION) {
		queue_event(EVENT_ORIENTATION_CHANGE, EVENT_SEVERITY_INFO,
			    (int16_t)motion->orientation);
	}

	if (motion->events & MOTION_EVENT_TAMPER) {
		status.tamper = 1U;
		status.alarm_flags |= ALARM_TAMPER;
		queue_event(EVENT_TAMPER, EVENT_SEVERITY_ALARM, 0);
	}

	status.motion_state = motion->motion_state;
	status.orientation = motion->orientation;
}

void app_process_sensor_data(sensor_data_t *data)
{
	struct app_subscriber *sub;
	struct motion_result motion;
	tag_config_t config;
	bool alarm;

	app_config_get(&config);

	/* Motion analysis first: it fills in the flags the log record wants. */
	motion_manager_process(data, &motion);

	k_mutex_lock(&lock, K_FOREVER);

	last_sample = *data;
	apply_motion_result(&motion);
	check_thresholds(data, &config);

	status.operating_mode = config.operating_mode;
	data->orientation = status.orientation;
	alarm = (status.alarm_flags != 0U);

	statistics.total_uptime_s = data->timestamp;

	k_mutex_unlock(&lock);

	if (config.sensor_history_enabled) {
		(void)sensor_log_add(data, alarm, NULL);
	}

	drain_pending_events();

	if (!alarm) {
		led_manager_set(LED_STATE_ALARM, false);
	}

	SYS_SLIST_FOR_EACH_CONTAINER(&subscribers, sub, node) {
		if (sub->on_sensor_data != NULL) {
			sub->on_sensor_data(data, sub->user_data);
		}
	}

	if ((data->timestamp - last_stats_flush) >= STATS_FLUSH_INTERVAL_S) {
		last_stats_flush = data->timestamp;
		app_state_flush_statistics();
	}
}

void app_state_get_status(tag_status_t *out)
{
	k_mutex_lock(&lock, K_FOREVER);
	*out = status;
	k_mutex_unlock(&lock);
}

void app_state_get_last_sample(sensor_data_t *out)
{
	k_mutex_lock(&lock, K_FOREVER);
	*out = last_sample;
	k_mutex_unlock(&lock);
}

void app_state_get_statistics(tag_statistics_t *out)
{
	k_mutex_lock(&lock, K_FOREVER);
	*out = statistics;
	k_mutex_unlock(&lock);
}

int app_state_set_time_utc(uint32_t utc_now)
{
	uint32_t uptime_s = (uint32_t)(k_uptime_get() / 1000);
	uint32_t epoch = smart_tag_boot_epoch_utc(utc_now, uptime_s);

	if (epoch == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&lock, K_FOREVER);
	statistics.boot_epoch_utc = epoch;
	k_mutex_unlock(&lock);

	app_state_flush_statistics();

	LOG_INF("boot epoch UTC %u (now %u, uptime %u s)", epoch, utc_now,
		uptime_s);

	(void)app_raise_event(EVENT_TIME_SET, EVENT_SEVERITY_INFO, 0);

	return 0;
}

int app_state_snapshot_gas_baseline(uint32_t *threshold_ohm)
{
	sensor_data_t sample;
	tag_config_t config;
	uint32_t floor;
	int err;

	app_state_get_last_sample(&sample);

	if ((sample.valid & SENSOR_VALID_GAS) == 0U ||
	    sample.gas_resistance_ohm == 0U) {
		return -ENOTSUP;
	}

	floor = smart_tag_gas_alarm_floor(sample.gas_resistance_ohm);
	if (floor == 0U) {
		floor = 1U;
	}

	app_config_get(&config);
	config.gas_low_threshold_ohm = floor;

	err = app_config_set(&config);
	if (err) {
		return err;
	}

	if (threshold_ohm != NULL) {
		*threshold_ohm = floor;
	}

	LOG_INF("gas alarm floor %u ohm (baseline %u)", floor,
		sample.gas_resistance_ohm);

	(void)app_raise_event(EVENT_CONFIG_CHANGED, EVENT_SEVERITY_INFO,
			      (int16_t)config.operating_mode);

	return 0;
}

void app_state_flush_statistics(void)
{
	tag_statistics_t snapshot;

	k_mutex_lock(&lock, K_FOREVER);
	snapshot = statistics;
	k_mutex_unlock(&lock);

	(void)config_storage_save_statistics(&snapshot);
}

void app_state_reset_counters(void)
{
	k_mutex_lock(&lock, K_FOREVER);

	status.shock_count = 0;
	status.free_fall_count = 0;
	status.event_count = 0;
	status.max_shock_mg = 0;
	status.movement_duration_s = 0;
	status.tamper = 0;
	status.alarm_flags &= (uint16_t)~(ALARM_SHOCK | ALARM_FREE_FALL |
					  ALARM_TAMPER);

	statistics.shock_count = 0;
	statistics.free_fall_count = 0;
	statistics.event_count = 0;
	statistics.max_shock_mg = 0;
	statistics.movement_duration_s = 0;

	k_mutex_unlock(&lock);

	motion_manager_clear_tamper();
	led_manager_set(LED_STATE_ALARM, false);

	app_state_flush_statistics();

	LOG_INF("counters reset");
}

void app_state_factory_reset(void)
{
	LOG_WRN("factory reset");

	led_manager_set(LED_STATE_ALARM, true);

	(void)app_raise_event(EVENT_FACTORY_RESET, EVENT_SEVERITY_WARNING, 0);

	(void)event_log_clear();
	(void)sensor_log_clear();
	(void)config_storage_factory_reset();
	(void)app_config_reset();

	k_sleep(K_MSEC(500));
	sys_reboot(SYS_REBOOT_WARM);
}
