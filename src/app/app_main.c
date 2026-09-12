/*
 * nRF54L15 Smart Tag - HOLyiot 25025 Beacon V1.0.
 *
 * Startup sequence follows the state machine in PLAN.md section 11:
 *
 *   BOOT -> INIT -> ZIGBEE_START -> RUNNING
 *
 * Order matters. Storage comes up before configuration, configuration before
 * the modules that cache thresholds, and the protocol stacks last so they can
 * subscribe to an application layer that is already consistent.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/settings/settings.h>

#include "board_id.h"
#include "smart_tag.h"
#include "app_state.h"
#include "app_config.h"
#include "sensors/sensor_manager.h"
#include "motion/motion_manager.h"
#include "storage/event_log.h"
#include "storage/sensor_log.h"
#include "storage/config_storage.h"
#include "ui/led_manager.h"
#include "power/power_manager.h"
#include "ble/ble_manager.h"
#include "zigbee/zigbee_manager.h"

LOG_MODULE_REGISTER(smart_tag, LOG_LEVEL_INF);

static const struct gpio_dt_spec button =
	GPIO_DT_SPEC_GET(DT_ALIAS(user_button), gpios);

/* Hold this long to wipe both logs and restore default settings. */
#define FACTORY_RESET_HOLD_MS 5000

static struct gpio_callback button_cb_data;
static struct k_work button_press_work;
static struct k_work factory_reset_work;
static struct k_timer factory_reset_timer;

static void factory_reset_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	if (IS_ENABLED(CONFIG_ZIGBEE_ADD_ON)) {
		zigbee_manager_factory_reset();
	}

	app_state_factory_reset();
}

static void factory_reset_timeout(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	k_work_submit(&factory_reset_work);
}

/*
 * PLAN.md section 11: BLE does not advertise continuously. A short press is
 * the "service me" gesture that opens the window.
 */
static void button_press_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_INF("button pressed: opening the BLE service window");

	power_manager_request_service_mode();
	power_manager_sample_now();
}

static void button_cb(const struct device *dev, struct gpio_callback *cb,
		      uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	if (gpio_pin_get_dt(&button) == 1) {
		/* Pressed: arm the factory-reset hold. */
		k_timer_start(&factory_reset_timer,
			      K_MSEC(FACTORY_RESET_HOLD_MS), K_NO_WAIT);
	} else if (k_timer_status_get(&factory_reset_timer) == 0) {
		/* Released before the hold elapsed: a short press. */
		k_timer_stop(&factory_reset_timer);
		k_work_submit(&button_press_work);
	}
}

static int button_init(void)
{
	int err;

	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("button GPIO not ready");
		return -ENODEV;
	}

	k_work_init(&button_press_work, button_press_work_fn);
	k_work_init(&factory_reset_work, factory_reset_work_fn);
	k_timer_init(&factory_reset_timer, factory_reset_timeout, NULL);

	err = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (err) {
		return err;
	}

	/*
	 * Both edges: press arms the hold timer, release cancels it. The
	 * button is on P1.13, and port P1 has GPIOTE - unlike the
	 * accelerometer interrupt lines on P2.
	 */
	err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
	if (err) {
		LOG_ERR("button interrupt not available: %d", err);
		return err;
	}

	gpio_init_callback(&button_cb_data, button_cb, BIT(button.pin));

	return gpio_add_callback(button.port, &button_cb_data);
}

static void report_identity(void)
{
	uint8_t jedec_id[3];
	int err;

	LOG_INF("=================================");
	LOG_INF("nRF54L15 Smart Tag");
	LOG_INF("%s, protocol v%u", SMART_TAG_MODEL_NAME,
		SMART_TAG_PROTOCOL_VERSION);
	LOG_INF("=================================");

	/*
	 * Only meaningful where an external flash is fitted. On the nRF54L15
	 * Tag the footprint is depopulated and the logs live in internal RRAM,
	 * so sensor_manager_flash_jedec_id() reports -ENOTSUP and there is
	 * nothing to print - not a failure worth a warning.
	 */
	err = sensor_manager_flash_jedec_id(jedec_id);
	if (err == 0) {
		LOG_INF("SPI NOR: JEDEC ID %02x %02x %02x", jedec_id[0],
			jedec_id[1], jedec_id[2]);
	} else if (err == -ENOTSUP) {
		LOG_INF("logs in internal RRAM (no external flash fitted)");
	} else {
		LOG_WRN("SPI NOR: JEDEC ID read failed: %d", err);
	}
}

int main(void)
{
	tag_config_t config;
	int err;

	/* --- BOOT ---------------------------------------------------- */
	led_manager_init();
	report_identity();

	/* --- INIT ---------------------------------------------------- */
	app_state_set(APP_STATE_INIT);

	if (button_init()) {
		LOG_ERR("button unavailable: BLE cannot be requested locally");
	}

	if (sensor_manager_init()) {
		LOG_WRN("one or more sensors are missing - continuing anyway");
	}

	err = settings_subsys_init();
	if (err) {
		LOG_ERR("settings init failed: %d", err);
	}

	config_storage_init();

	if (event_log_init()) {
		LOG_ERR("event log unavailable");
	}

	if (sensor_log_init()) {
		LOG_ERR("sensor history unavailable");
	}

	/*
	 * Loads the configuration and the statistics, so it has to run
	 * before anything reads them. Deliberately NOT a blanket
	 * settings_load(): that also restores Bluetooth's
	 * statically-registered handlers (e.g. the GATT service-changed
	 * state), which run regardless of whether bt_enable() has been
	 * called yet. This runs before ble_manager_init()'s bt_enable(), so
	 * a blanket load here ends up scheduling BT's internal delayed work
	 * before bt_gatt_init() has initialized it - the work item is
	 * processed later with a NULL handler, and the system workqueue
	 * crashes with a USAGE FAULT when it gets to it. The Bluetooth keys
	 * are loaded separately, correctly, by ble_manager_init()'s own
	 * settings_load_subtree("bt") after bt_enable() - see
	 * CONFIG_STORAGE_SETTINGS_ROOT's comment in config_storage.h.
	 */
	err = settings_load_subtree(CONFIG_STORAGE_SETTINGS_ROOT);
	if (err) {
		LOG_ERR("settings load failed: %d", err);
	}

	app_config_init();
	app_config_get(&config);

	app_state_init();
	motion_manager_init(&config);
	power_manager_init();

	led_manager_set_enabled(config.led_enabled != 0U);

	(void)app_raise_event(EVENT_BOOT, EVENT_SEVERITY_INFO,
			      SMART_TAG_PROTOCOL_VERSION);

	/* --- ZIGBEE_START -------------------------------------------- */
	app_state_set(APP_STATE_ZIGBEE_START);

	err = ble_manager_init();
	if (err) {
		LOG_ERR("BLE init failed: %d", err);
	}

	/* Returns -ENOTSUP until the Zigbee R23 add-on is in the workspace. */
	err = zigbee_manager_init();
	if (err && err != -ENOTSUP) {
		LOG_ERR("Zigbee init failed: %d", err);
	}

	/* --- RUNNING ------------------------------------------------- */
	power_manager_start();

	if (IS_ENABLED(CONFIG_SMART_TAG_BLE_ADVERTISE_AT_BOOT)) {
		LOG_INF("CONFIG_SMART_TAG_BLE_ADVERTISE_AT_BOOT: opening the BLE service window at boot");
		power_manager_request_service_mode();
	}

	/*
	 * Everything from here is timer, interrupt and workqueue driven.
	 * main returns so its stack can be reclaimed.
	 */
	return 0;
}
