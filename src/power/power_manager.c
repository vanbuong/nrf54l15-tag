#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include "power_manager.h"
#include "watchdog.h"
#include "app/app_state.h"
#include "app/app_config.h"
#include "sensors/sensor_manager.h"
#include "ble/ble_manager.h"

LOG_MODULE_REGISTER(power_manager, LOG_LEVEL_INF);

#define SAMPLING_STACK_SIZE 2048
#define SAMPLING_PRIORITY   7

static K_THREAD_STACK_DEFINE(sampling_stack, SAMPLING_STACK_SIZE);
static struct k_thread sampling_thread;

/* Given to cut a sleep short when someone wants a sample now. */
static K_SEM_DEFINE(sample_now, 0, 1);

/*
 * Set by the accelerometer's activity interrupt, cleared once the sample it
 * asked for has been taken. Only meaningful on a board that has a wake
 * source; where sensor_manager_wake_source() returns NULL this stays false
 * and the loop behaves exactly as it always did.
 */
static atomic_t motion_pending;

static struct k_work_delayable service_timeout_work;
static bool service_mode;
static bool started;

static void service_timeout_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_INF("BLE service window expired");
	power_manager_end_service_mode();
}

/*
 * How long to sleep before the next sample.
 *
 * app_config_effective_interval_s() divides the configured interval by an
 * operating-mode factor - 4x in SHIPPING, 2x in ASSET. That divider existed
 * because the HOLyiot board could not be woken by its accelerometer, so
 * sampling more often was the only way to cut motion latency: the interval
 * WAS the latency.
 *
 * With a wake source that is no longer true, and the divider can go back to
 * meaning what it says. While the tag is moving, sample at the mode's faster
 * rate, because that is when shock and orientation data is worth having.
 * While it is stationary, sample at the plain configured interval and let the
 * interrupt handle latency - there is nothing to catch between samples that
 * the accelerometer will not raise by itself.
 *
 * On a board with no wake source this returns the old value unchanged.
 */
static uint16_t sleep_interval_s(void)
{
	tag_status_t status;
	tag_config_t config;

	if (sensor_manager_wake_source() == NULL) {
		return app_config_effective_interval_s();
	}

	app_state_get_status(&status);

	if (status.motion_state == MOTION_STATE_MOVING) {
		return app_config_effective_interval_s();
	}

	app_config_get(&config);

	return MAX(config.sampling_interval_s, 1);
}

/*
 * Accelerometer activity interrupt. Runs on the sensor driver's global
 * workqueue thread (CONFIG_ADXL367_TRIGGER_GLOBAL_THREAD), not in ISR
 * context, but it is kept to a semaphore give anyway so the driver thread is
 * never held up behind a full sensor read.
 */
static void motion_trigger_fn(const struct device *dev,
			      const struct sensor_trigger *trig)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(trig);

	atomic_set(&motion_pending, 1);
	k_sem_give(&sample_now);
}

static void motion_trigger_init(void)
{
	const struct device *wake = sensor_manager_wake_source();
	struct sensor_trigger trig = {
		.type = SENSOR_TRIG_THRESHOLD,
		.chan = SENSOR_CHAN_ACCEL_XYZ,
	};

	if (wake == NULL) {
		/*
		 * No wake source fitted - the HOLyiot board, whose
		 * accelerometer interrupts land on port P2 where there is no
		 * GPIOTE. Motion latency there is the sampling interval.
		 */
		LOG_INF("no motion wake source: motion latency is the sampling interval");
		return;
	}

	if (!device_is_ready(wake)) {
		LOG_ERR("motion wake source not ready");
		return;
	}

	if (sensor_trigger_set(wake, &trig, motion_trigger_fn) != 0) {
		LOG_ERR("could not arm the motion trigger; falling back to polling");
		return;
	}

	LOG_INF("motion wake armed on %s", wake->name);
}

static void sampling_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	motion_trigger_init();

	while (true) {
		sensor_data_t data;

		atomic_set(&motion_pending, 0);

		sensor_manager_read(&data);
		app_process_sensor_data(&data);
		smart_tag_watchdog_feed();

		/*
		 * k_sem_take with a timeout is the sleep. The idle thread puts
		 * the SoC into its lowest available mode while nothing runs,
		 * so this is the tag's resting state.
		 *
		 * Two things end it: the interval expiring, or the semaphore
		 * being given. The accelerometer's activity interrupt gives it,
		 * so on a board with a wake source motion is now reacted to in
		 * milliseconds instead of waiting out the interval. That is the
		 * whole benefit of the ADXL367 sitting on P0.03 rather than on
		 * a port with no GPIOTE.
		 */
		(void)k_sem_take(&sample_now, K_SECONDS(sleep_interval_s()));
	}
}

int power_manager_init(void)
{
	k_work_init_delayable(&service_timeout_work, service_timeout_fn);

	return 0;
}

void power_manager_start(void)
{
	if (started) {
		return;
	}

	started = true;

	k_thread_create(&sampling_thread, sampling_stack,
			K_THREAD_STACK_SIZEOF(sampling_stack),
			sampling_thread_fn, NULL, NULL, NULL, SAMPLING_PRIORITY,
			0, K_NO_WAIT);
	k_thread_name_set(&sampling_thread, "sampling");

	app_state_set(APP_STATE_RUNNING);

	LOG_INF("sampling every %u s (%s)", app_config_effective_interval_s(),
		sensor_manager_wake_source() != NULL
			? "or sooner, on motion"
			: "polled; no motion wake source on this board");
}

void power_manager_sample_now(void)
{
	k_sem_give(&sample_now);
}

void power_manager_config_changed(void)
{
	/* Wake the thread so the new interval takes effect immediately. */
	k_sem_give(&sample_now);
}

void power_manager_request_service_mode(void)
{
	tag_config_t config;

	app_config_get(&config);

	if (!service_mode) {
		service_mode = true;
		LOG_INF("BLE service window open for %u s",
			config.ble_service_window_s);

		if (ble_manager_start_advertising() != 0) {
			service_mode = false;
			return;
		}

		app_state_set(APP_STATE_SERVICE);
	}

	/* Restart the window on every request, so activity extends it. */
	k_work_reschedule(&service_timeout_work,
			  K_SECONDS(config.ble_service_window_s));
}

void power_manager_end_service_mode(void)
{
	if (!service_mode) {
		return;
	}

	(void)k_work_cancel_delayable(&service_timeout_work);

	/*
	 * Leave a live connection alone - the phone is mid-download. The
	 * window is restarted on disconnect instead.
	 */
	if (ble_manager_is_connected()) {
		LOG_DBG("service window held open by an active connection");
		k_work_reschedule(&service_timeout_work, K_SECONDS(30));
		return;
	}

	service_mode = false;
	ble_manager_stop_advertising();
	app_state_set(APP_STATE_RUNNING);
}

bool power_manager_service_mode_active(void)
{
	return service_mode;
}
