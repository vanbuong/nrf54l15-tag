/*
 * Hardware validation firmware - PLAN.md phase 1.
 *
 * Proves every peripheral on the board before any protocol stack is layered
 * on top. Built with the validation configuration:
 *
 *     west build -b <board> -p always . -- -DCONF_FILE=prj_validation.conf
 *
 * This file is the shared half: the result harness, and the tests that are
 * the same on both supported boards - LEDs, button, battery and log storage.
 * The bus and sensor tests differ completely between them and live in
 * hw_validation_holyiot.c and hw_validation_tag.c, reached through
 * hw_validation_board_tests(). See hw_validation.h.
 *
 * Everything uses raw register access rather than the Zephyr sensor drivers,
 * on purpose - see hw_validation.h for why.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "hw_validation.h"

LOG_MODULE_REGISTER(hw_validation, LOG_LEVEL_DBG);

#define record hw_validation_record

/* --------------------------------------------------------------------------
 * Devices, taken straight from the board device tree
 * ------------------------------------------------------------------------ */

static const struct gpio_dt_spec led_red =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_red), gpios);
static const struct gpio_dt_spec led_green =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_green), gpios);
static const struct gpio_dt_spec led_blue =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_blue), gpios);
static const struct gpio_dt_spec button =
	GPIO_DT_SPEC_GET(DT_ALIAS(user_button), gpios);

static const struct adc_dt_spec battery_adc =
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

/* --------------------------------------------------------------------------
 * Result bookkeeping
 * ------------------------------------------------------------------------ */

struct test_result {
	const char *name;
	result_t result;
	char detail[64];
};

#define MAX_TESTS 16

static struct test_result results[MAX_TESTS];
static size_t result_count;

void hw_validation_record(const char *name, result_t result, const char *fmt,
			  ...)
{
	va_list args;

	if (result_count >= MAX_TESTS) {
		return;
	}

	results[result_count].name = name;
	results[result_count].result = result;

	va_start(args, fmt);
	vsnprintk(results[result_count].detail,
		  sizeof(results[result_count].detail), fmt, args);
	va_end(args);

	result_count++;
}

static const char *result_text(result_t result)
{
	switch (result) {
	case RESULT_PASS:
		return "PASS";
	case RESULT_FAIL:
		return "FAIL";
	case RESULT_SKIP:
		return "SKIP";
	default:
		return "CHECK";
	}
}

/* --------------------------------------------------------------------------
 * Test: LEDs
 * ------------------------------------------------------------------------ */

static void test_leds(void)
{
	static const struct {
		const char *name;
		bool r, g, b;
	} sequence[] = {
		{ "red", true, false, false },
		{ "green", false, true, false },
		{ "blue", false, false, true },
		{ "yellow", true, true, false },
		{ "cyan", false, true, true },
		{ "magenta", true, false, true },
		{ "white", true, true, true },
	};

	if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green) ||
	    !gpio_is_ready_dt(&led_blue)) {
		record("RGB LED", RESULT_FAIL, "GPIO port not ready");
		return;
	}

	gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);

	LOG_INF("LED sequence starting - watch the tag");

	for (size_t i = 0; i < ARRAY_SIZE(sequence); i++) {
		LOG_INF("  LED should now be %s", sequence[i].name);
		gpio_pin_set_dt(&led_red, sequence[i].r);
		gpio_pin_set_dt(&led_green, sequence[i].g);
		gpio_pin_set_dt(&led_blue, sequence[i].b);
		k_sleep(K_MSEC(700));
	}

	gpio_pin_set_dt(&led_red, 0);
	gpio_pin_set_dt(&led_green, 0);
	gpio_pin_set_dt(&led_blue, 0);

	/*
	 * Nothing reads the LED back, so the firmware cannot know whether the
	 * colours appeared. Reported as needing a human.
	 */
	record("RGB LED", RESULT_MANUAL, "7 colours shown, confirm by eye");
}

/* --------------------------------------------------------------------------
 * Test: button
 * ------------------------------------------------------------------------ */

#define BUTTON_WAIT_S 10

static void test_button(void)
{
	int64_t deadline;
	int idle;

	if (!gpio_is_ready_dt(&button)) {
		record("Button", RESULT_FAIL, "GPIO port not ready");
		return;
	}

	if (gpio_pin_configure_dt(&button, GPIO_INPUT) != 0) {
		record("Button", RESULT_FAIL, "cannot configure as input");
		return;
	}

	/* Released should read inactive: 100k pull-up, switch to ground. */
	idle = gpio_pin_get_dt(&button);
	if (idle != 0) {
		record("Button", RESULT_FAIL,
		       "reads pressed while idle (stuck low?)");
		return;
	}

	LOG_INF("Press the button within %d s", BUTTON_WAIT_S);
	deadline = k_uptime_get() + (BUTTON_WAIT_S * 1000);

	while (k_uptime_get() < deadline) {
		if (gpio_pin_get_dt(&button) == 1) {
			LOG_INF("  button press detected");

			/* Confirm it releases too, so a short is not a pass. */
			while (gpio_pin_get_dt(&button) == 1 &&
			       k_uptime_get() < deadline) {
				k_sleep(K_MSEC(10));
			}

			if (gpio_pin_get_dt(&button) == 0) {
				record("Button", RESULT_PASS,
				       "press and release seen");
			} else {
				record("Button", RESULT_FAIL,
				       "pressed but never released");
			}
			return;
		}

		k_sleep(K_MSEC(20));
	}

	record("Button", RESULT_FAIL, "no press within %d s", BUTTON_WAIT_S);
}


#ifdef CONFIG_SMART_TAG_HW_VALIDATION_DESTRUCTIVE

/*
 * Erase, blank-check, write and verify one sector. Uses the event log
 * partition because it is a log: losing its first sector during bring-up
 * costs nothing.
 *
 * Media-agnostic on purpose. This is external SPI NOR on the HOLyiot board
 * and internal RRAM on the nRF54L15 Tag, whose flash footprint is not
 * populated - but flash_manager treats both through the flash_area API, so
 * proving the API works on whatever is underneath is exactly the point.
 */
static void test_log_readwrite(void)
{
	static const uint8_t pattern[] = { 0x53, 0x6d, 0x61, 0x72, 0x74,
					   0x54, 0x61, 0x67 };
	const struct flash_parameters *params;
	const struct flash_area *area;
	struct flash_pages_info page;
	uint8_t buffer[sizeof(pattern)];
	uint8_t blank;
	int err;

	err = flash_area_open(PARTITION_ID(event_log_partition), &area);
	if (err) {
		record("Log R/W", RESULT_FAIL, "cannot open partition (%d)",
		       err);
		return;
	}

	err = flash_get_page_info_by_offs(flash_area_get_device(area),
					  area->fa_off, &page);
	if (err) {
		record("Log R/W", RESULT_FAIL, "no page info (%d)", err);
		goto out;
	}

	LOG_INF("Erasing %u bytes at the start of the event log partition",
		(unsigned int)page.size);

	err = flash_area_erase(area, 0, page.size);
	if (err) {
		record("Log R/W", RESULT_FAIL, "erase failed (%d)", err);
		goto out;
	}

	err = flash_area_read(area, 0, buffer, sizeof(buffer));
	if (err) {
		record("Log R/W", RESULT_FAIL, "read after erase failed (%d)",
		       err);
		goto out;
	}

	/*
	 * The blank value comes from the driver rather than being assumed to
	 * be 0xff, since this runs on two different storage technologies.
	 */
	params = flash_get_parameters(flash_area_get_device(area));
	blank = (params != NULL) ? params->erase_value : 0xffU;

	for (size_t i = 0; i < sizeof(buffer); i++) {
		if (buffer[i] != blank) {
			record("Log R/W", RESULT_FAIL,
			       "not blank after erase (byte %u = 0x%02x)",
			       (unsigned int)i, buffer[i]);
			goto out;
		}
	}

	err = flash_area_write(area, 0, pattern, sizeof(pattern));
	if (err) {
		record("Log R/W", RESULT_FAIL, "write failed (%d)", err);
		goto out;
	}

	err = flash_area_read(area, 0, buffer, sizeof(buffer));
	if (err) {
		record("Log R/W", RESULT_FAIL, "read back failed (%d)",
		       err);
		goto out;
	}

	if (memcmp(buffer, pattern, sizeof(pattern)) != 0) {
		record("Log R/W", RESULT_FAIL, "read back does not match");
		goto out;
	}

	/* Leave the sector blank so the log starts clean. */
	(void)flash_area_erase(area, 0, page.size);

	record("Log R/W", RESULT_PASS, "erase, write and verify on a %u B sector",
	       (unsigned int)page.size);

out:
	flash_area_close(area);
}

#else /* !CONFIG_SMART_TAG_HW_VALIDATION_DESTRUCTIVE */

static void test_log_readwrite(void)
{
	record("Log R/W", RESULT_SKIP, "destructive test disabled");
}

#endif

static void test_log_map(void)
{
	const struct flash_area *events;
	const struct flash_area *history;
	int err;

	err = flash_area_open(PARTITION_ID(event_log_partition), &events);
	if (err) {
		record("Log map", RESULT_FAIL, "event log missing (%d)", err);
		return;
	}

	err = flash_area_open(PARTITION_ID(sensor_log_partition),
			      &history);
	if (err) {
		flash_area_close(events);
		record("Log map", RESULT_FAIL, "sensor history missing (%d)",
		       err);
		return;
	}

	LOG_INF("event log     @ 0x%08lx, %u KB",
		(unsigned long)events->fa_off,
		(unsigned int)(events->fa_size / 1024));
	LOG_INF("sensor history@ 0x%08lx, %u KB",
		(unsigned long)history->fa_off,
		(unsigned int)(history->fa_size / 1024));

	record("Log map", RESULT_PASS, "event %u KB, history %u KB",
	       (unsigned int)(events->fa_size / 1024),
	       (unsigned int)(history->fa_size / 1024));

	flash_area_close(events);
	flash_area_close(history);
}

/* --------------------------------------------------------------------------
 * Test: battery
 * ------------------------------------------------------------------------ */

static void test_battery(void)
{
	int16_t raw = 0;
	int32_t millivolts;
	int err;

	struct adc_sequence sequence = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};

	if (!adc_is_ready_dt(&battery_adc)) {
		record("Battery", RESULT_FAIL, "SAADC not ready");
		return;
	}

	err = adc_channel_setup_dt(&battery_adc);
	if (err) {
		record("Battery", RESULT_FAIL, "channel setup failed (%d)",
		       err);
		return;
	}

	err = adc_sequence_init_dt(&battery_adc, &sequence);
	if (err) {
		record("Battery", RESULT_FAIL, "sequence init failed (%d)",
		       err);
		return;
	}

	err = adc_read_dt(&battery_adc, &sequence);
	if (err) {
		record("Battery", RESULT_FAIL, "read failed (%d)", err);
		return;
	}

	millivolts = raw;

	err = adc_raw_to_millivolts_dt(&battery_adc, &millivolts);
	if (err) {
		record("Battery", RESULT_FAIL, "conversion failed (%d)", err);
		return;
	}

	LOG_INF("Battery (VDD): raw %d, %d mV", raw, millivolts);

	/*
	 * A CR2032 is 3.0 V nominal and dead below about 2.0 V; a debugger
	 * supply reads around 3.3 V. Anything outside that band means the
	 * channel is not measuring what we think it is.
	 */
	if (millivolts < 1800 || millivolts > 3600) {
		record("Battery", RESULT_FAIL, "%d mV is outside 1800-3600 mV",
		       millivolts);
		return;
	}

	record("Battery", RESULT_PASS, "%d mV", millivolts);
}


/* --------------------------------------------------------------------------
 * Report
 * ------------------------------------------------------------------------ */

static void print_report(void)
{
	unsigned int passed = 0;
	unsigned int failed = 0;
	unsigned int manual = 0;
	unsigned int skipped = 0;

	LOG_INF("");
	LOG_INF("================ VALIDATION REPORT ================");

	for (size_t i = 0; i < result_count; i++) {
		LOG_INF("%-5s  %-14s  %s", result_text(results[i].result),
			results[i].name, results[i].detail);

		switch (results[i].result) {
		case RESULT_PASS:
			passed++;
			break;
		case RESULT_FAIL:
			failed++;
			break;
		case RESULT_MANUAL:
			manual++;
			break;
		default:
			skipped++;
			break;
		}
	}

	LOG_INF("---------------------------------------------------");
	LOG_INF("%u passed, %u failed, %u to confirm by eye, %u skipped",
		passed, failed, manual, skipped);
	LOG_INF("===================================================");
	LOG_INF("");

	if (failed > 0) {
		LOG_ERR("Hardware validation FAILED - do not move on to phase 2");
	} else {
		LOG_INF("Hardware validation passed");
	}

	/* Steady red on any failure, steady green otherwise. */
	if (gpio_is_ready_dt(&led_red)) {
		gpio_pin_set_dt(&led_red, failed > 0);
		gpio_pin_set_dt(&led_green, failed == 0);
		gpio_pin_set_dt(&led_blue, 0);
	}
}

static void run_all_tests(void)
{
	result_count = 0;

	test_leds();
	hw_validation_board_tests();
	test_log_map();
	test_log_readwrite();
	test_battery();
	test_button();

	print_report();
}

int main(void)
{
	LOG_INF("===================================================");
	LOG_INF("nRF54L15 Smart Tag - hardware validation");
	LOG_INF("Board: %s", CONFIG_BOARD_TARGET);
	LOG_INF("===================================================");
	LOG_INF("");

	run_all_tests();

	if (!gpio_is_ready_dt(&button)) {
		LOG_WRN("button unavailable - reset the board to run again");
		return 0;
	}

	LOG_INF("Press the button to run the tests again");

	while (true) {
		if (gpio_pin_get_dt(&button) == 1) {
			while (gpio_pin_get_dt(&button) == 1) {
				k_sleep(K_MSEC(20));
			}

			LOG_INF("");
			LOG_INF("Re-running validation");
			LOG_INF("");

			run_all_tests();
			LOG_INF("Press the button to run the tests again");
		}

		k_sleep(K_MSEC(50));
	}
}
