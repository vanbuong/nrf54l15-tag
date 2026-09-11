#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "led_manager.h"

LOG_MODULE_REGISTER(led_manager, LOG_LEVEL_INF);

/* The RGB LED is common anode; the device tree marks all three active low. */
static const struct gpio_dt_spec led_red =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_red), gpios);
static const struct gpio_dt_spec led_green =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_green), gpios);
static const struct gpio_dt_spec led_blue =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_blue), gpios);

#define COLOUR_OFF	0x0
#define COLOUR_RED	0x1
#define COLOUR_GREEN	0x2
#define COLOUR_BLUE	0x4
#define COLOUR_YELLOW	(COLOUR_RED | COLOUR_GREEN)
#define COLOUR_PURPLE	(COLOUR_RED | COLOUR_BLUE)
#define COLOUR_WHITE	(COLOUR_RED | COLOUR_GREEN | COLOUR_BLUE)

/*
 * A coin cell cannot run an LED continuously, so every indication is a short
 * blink on a long period. Only identify is bright and fast, because a person
 * is looking for it.
 */
struct led_pattern {
	uint8_t colour;
	uint16_t on_ms;
	uint16_t period_ms;
};

static const struct led_pattern patterns[LED_STATE_COUNT] = {
	[LED_STATE_IDLE] = { COLOUR_OFF, 0, 0 },
	[LED_STATE_BLE_ADVERTISING] = { COLOUR_BLUE, 30, 2000 },
	[LED_STATE_ZIGBEE_JOINING] = { COLOUR_YELLOW, 100, 500 },
	[LED_STATE_ZIGBEE_CONNECTED] = { COLOUR_GREEN, 20, 5000 },
	[LED_STATE_OTA] = { COLOUR_PURPLE, 100, 300 },
	[LED_STATE_ALARM] = { COLOUR_RED, 100, 1000 },
	[LED_STATE_IDENTIFY] = { COLOUR_WHITE, 250, 500 },
};

static bool ready;
static bool enabled = true;
static uint32_t active_mask;	/* bit per led_state_t */

static struct k_work_delayable blink_work;
static struct k_work_delayable identify_timeout_work;

/* One-off acknowledgement flashes, independent of the active set. */
static uint8_t flash_remaining;
static uint8_t flash_colour;

static bool light_on;

static void drive(uint8_t colour)
{
	if (!ready) {
		return;
	}

	gpio_pin_set_dt(&led_red, (colour & COLOUR_RED) ? 1 : 0);
	gpio_pin_set_dt(&led_green, (colour & COLOUR_GREEN) ? 1 : 0);
	gpio_pin_set_dt(&led_blue, (colour & COLOUR_BLUE) ? 1 : 0);
}

/* Highest-priority active indication, or LED_STATE_IDLE if none. */
static led_state_t top_state(void)
{
	for (int i = LED_STATE_COUNT - 1; i > LED_STATE_IDLE; i--) {
		if (active_mask & BIT(i)) {
			return (led_state_t)i;
		}
	}

	return LED_STATE_IDLE;
}

static void blink_work_fn(struct k_work *work)
{
	const struct led_pattern *pattern;
	led_state_t state;

	ARG_UNUSED(work);

	if (!enabled) {
		drive(COLOUR_OFF);
		return;
	}

	/* Acknowledgement flashes pre-empt the steady pattern briefly. */
	if (flash_remaining > 0U) {
		light_on = !light_on;
		drive(light_on ? flash_colour : COLOUR_OFF);
		flash_remaining--;
		k_work_reschedule(&blink_work, K_MSEC(80));
		return;
	}

	state = top_state();
	pattern = &patterns[state];

	if (state == LED_STATE_IDLE || pattern->period_ms == 0U) {
		light_on = false;
		drive(COLOUR_OFF);
		return;
	}

	if (light_on) {
		light_on = false;
		drive(COLOUR_OFF);
		k_work_reschedule(&blink_work,
				  K_MSEC(pattern->period_ms - pattern->on_ms));
	} else {
		light_on = true;
		drive(pattern->colour);
		k_work_reschedule(&blink_work, K_MSEC(pattern->on_ms));
	}
}

static void identify_timeout_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	led_manager_set(LED_STATE_IDENTIFY, false);
}

int led_manager_init(void)
{
	if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green) ||
	    !gpio_is_ready_dt(&led_blue)) {
		LOG_ERR("LED GPIOs not ready");
		return -ENODEV;
	}

	gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);

	k_work_init_delayable(&blink_work, blink_work_fn);
	k_work_init_delayable(&identify_timeout_work, identify_timeout_fn);

	ready = true;

	return 0;
}

void led_manager_set(led_state_t state, bool active)
{
	if (!ready || state >= LED_STATE_COUNT) {
		return;
	}

	if (active) {
		active_mask |= BIT(state);
	} else {
		active_mask &= ~BIT(state);
	}

	/* Restart the pattern so a higher-priority state shows immediately. */
	light_on = false;
	k_work_reschedule(&blink_work, K_NO_WAIT);
}

void led_manager_identify(uint16_t duration_s)
{
	if (!ready) {
		return;
	}

	led_manager_set(LED_STATE_IDENTIFY, true);
	k_work_reschedule(&identify_timeout_work, K_SECONDS(duration_s));
}

void led_manager_flash(led_state_t state, uint8_t count)
{
	if (!ready || !enabled || state >= LED_STATE_COUNT || count == 0U) {
		return;
	}

	flash_colour = patterns[state].colour;
	flash_remaining = (uint8_t)MIN(count * 2U, UINT8_MAX);
	light_on = false;

	k_work_reschedule(&blink_work, K_NO_WAIT);
}

void led_manager_set_enabled(bool value)
{
	enabled = value;

	if (!enabled) {
		flash_remaining = 0;
		light_on = false;
		drive(COLOUR_OFF);
	} else {
		k_work_reschedule(&blink_work, K_NO_WAIT);
	}
}
