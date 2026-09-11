#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <string.h>

#include "app_state.h"
#include "app_config.h"
#include "../motion/motion_manager.h"
#include "../sensors/sensor_manager.h"
#include "../storage/event_log.h"
#include "../storage/sensor_log.h"
#include "../storage/config_storage.h"
#include "../ui/led_manager.h"

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
	uint8_t expected;

	if ((data->valid & SENSOR_VALID_TEMP_RH) != 0U) {
		if (data->temperature_c_x100 > config->temp_high_c_x100) {
			if ((status.alarm_flags & ALARM_TEMP_HIGH) == 0U) {
				status.alarm_flags |= ALARM_TEMP_HIGH;
				queue_event(EVENT_TEMP_HIGH,
					    EVENT_SEVERITY_ALARM,
					    data->temperature_c_x100);
			}
		} else if (data->temperature_c_x100 < config->temp_low_c_x100) {
			if ((status.alarm_flags & ALARM_TEMP_LOW) == 0U) {
				status.alarm_flags |= ALARM_TEMP_LOW;
				queue_event(EVENT_TEMP_LOW,
					    EVENT_SEVERITY_ALARM,
					    data->temperature_c_x100);
			}
		} else {
			status.alarm_flags &=
				(uint16_t)~(ALARM_TEMP_HIGH | ALARM_TEMP_LOW);
		}

		if (data->humidity_x100 > config->humidity_high_x100) {
			if ((status.alarm_flags & ALARM_HUMIDITY_HIGH) == 0U) {
				status.alarm_flags |= ALARM_HUMIDITY_HIGH;
				queue_event(EVENT_HUMIDITY_HIGH,
					    EVENT_SEVERITY_WARNING,
					    (int16_t)data->humidity_x100);
			}
		} else {
			status.alarm_flags &= (uint16_t)~ALARM_HUMIDITY_HIGH;
		}
	}

	/*
	 * Gas, protocol v3. Note the inverted sense: the BME688 reports a
	 * resistance that FALLS as VOC concentration rises, so the alarm is a
	 * floor, not a ceiling like temperature and humidity above.
	 *
	 * A zero threshold disables the check, which is the default: the
	 * absolute resistance depends on the individual sensor and on how long
	 * it has burned in, so a meaningful floor has to be set per unit after
	 * commissioning rather than shipped as a constant.
	 *
	 * The event value is the reading in kOhm - gas_resistance_ohm is 32-bit
	 * and event_record_t.value is an int16_t, so ohms would overflow it.
	 */
	if ((data->valid & SENSOR_VALID_GAS) != 0U &&
	    config->gas_low_threshold_ohm != 0U) {
		if (data->gas_resistance_ohm < config->gas_low_threshold_ohm) {
			if ((status.alarm_flags & ALARM_GAS_LOW) == 0U) {
				status.alarm_flags |= ALARM_GAS_LOW;
				queue_event(EVENT_GAS_LOW,
					    EVENT_SEVERITY_WARNING,
					    (int16_t)MIN(data->gas_resistance_ohm / 1000U,
							 (uint32_t)INT16_MAX));
			}
		} else {
			status.alarm_flags &= (uint16_t)~ALARM_GAS_LOW;
		}
	}

	if ((data->valid & SENSOR_VALID_BATTERY) != 0U) {
		if (data->battery_mv < config->battery_low_mv) {
			if ((status.alarm_flags & ALARM_BATTERY_LOW) == 0U) {
				status.alarm_flags |= ALARM_BATTERY_LOW;
				queue_event(EVENT_BATTERY_LOW,
					    EVENT_SEVERITY_WARNING,
					    (int16_t)data->battery_mv);
			}
		}

		if (statistics.min_battery_mv == 0U ||
		    data->battery_mv < statistics.min_battery_mv) {
			statistics.min_battery_mv = data->battery_mv;
		}
	}

	/*
	 * Compare against what this board's fitted sensors can actually
	 * produce, not a fixed set. This used to be a hard-coded four-bit
	 * constant, which only worked while there was exactly one sensor set;
	 * it would latch ALARM_SENSOR_FAULT forever on any board that has
	 * different sensors - including one that reports MORE, since the test
	 * was an equality.
	 *
	 * Subset, not equality: extra bits (the gyroscope, which is only read
	 * while moving) are not a fault.
	 */
	expected = sensor_manager_expected_valid();

	if ((data->valid & expected) != expected) {
		if ((status.alarm_flags & ALARM_SENSOR_FAULT) == 0U) {
			status.alarm_flags |= ALARM_SENSOR_FAULT;
			statistics.sensor_faults++;
			queue_event(EVENT_SENSOR_FAULT, EVENT_SEVERITY_WARNING,
				    (int16_t)data->valid);
		}
	} else {
		status.alarm_flags &= (uint16_t)~ALARM_SENSOR_FAULT;
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
