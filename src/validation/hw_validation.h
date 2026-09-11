/*
 * Hardware validation harness - PLAN.md phase 1.
 *
 * The test runner is split in two because the two supported boards share no
 * sensors at all:
 *
 *   hw_validation.c          the harness, and every test that is the same on
 *                            both boards - LEDs, button, battery, log storage
 *   hw_validation_<board>.c  the sensor and bus tests for one board
 *
 * Everything here uses raw register access rather than the Zephyr sensor
 * drivers, on purpose. During bring-up "the driver did not bind" collapses
 * several very different faults - wrong address, wrong part fitted, dead bus,
 * bad pinctrl - into one message. A chip-ID read tells you which, and it
 * still works when the fitted part is not the one the driver expects.
 */
#ifndef HW_VALIDATION_H
#define HW_VALIDATION_H

#include <zephyr/kernel.h>

typedef enum {
	RESULT_PASS = 0,
	RESULT_FAIL,
	RESULT_SKIP,
	RESULT_MANUAL,	/* needs a human to confirm what they saw */
} result_t;

/**
 * Records one test outcome for the final report. Safe to call more times
 * than the report can hold; the extras are dropped rather than overflowing.
 */
void hw_validation_record(const char *name, result_t result, const char *fmt,
			  ...);

/**
 * The board-specific half of the suite: buses and sensors. Implemented once
 * per board, selected in CMakeLists.txt by which sensor aliases exist.
 * Called after the LED test and before the storage and battery tests.
 */
void hw_validation_board_tests(void);

#endif /* HW_VALIDATION_H */
