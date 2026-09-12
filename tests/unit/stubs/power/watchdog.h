/*
 * Host-test stand-in. Firmware uses src/power/watchdog.h; the unit-test
 * include path puts stubs/ first so flash_manager.c can call feed()
 * without linking the Zephyr watchdog driver.
 */
#ifndef SMART_TAG_WATCHDOG_H
#define SMART_TAG_WATCHDOG_H

static inline int smart_tag_watchdog_init(void)
{
	return 0;
}

static inline void smart_tag_watchdog_feed(void)
{
}

#endif
