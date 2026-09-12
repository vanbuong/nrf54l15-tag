#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>

#include "watchdog.h"

LOG_MODULE_REGISTER(watchdog, LOG_LEVEL_INF);

/* CPU-time budget. Sleep does not count when PAUSE_IN_SLEEP is honoured. */
#define WDT_WINDOW_MS 8000

static const struct device *wdt_dev;
static int wdt_chan = -1;

int smart_tag_watchdog_init(void)
{
	struct wdt_timeout_cfg cfg = {
		.window = { .min = 0, .max = WDT_WINDOW_MS },
		.callback = NULL,
		.flags = WDT_FLAG_RESET_SOC,
	};
	int err;

	wdt_dev = NULL;
	wdt_chan = -1;

#if DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
	wdt_dev = DEVICE_DT_GET(DT_ALIAS(watchdog0));
	if (!device_is_ready(wdt_dev)) {
		LOG_WRN("watchdog device not ready - continuing without it");
		wdt_dev = NULL;
		return 0;
	}

	wdt_chan = wdt_install_timeout(wdt_dev, &cfg);
	if (wdt_chan < 0) {
		LOG_ERR("watchdog install failed: %d", wdt_chan);
		return wdt_chan;
	}

	err = wdt_setup(wdt_dev, WDT_OPT_PAUSE_IN_SLEEP |
					 WDT_OPT_PAUSE_HALTED_BY_DBG);
	if (err) {
		LOG_WRN("watchdog pause-in-sleep not supported (%d), trying without",
			err);
		err = wdt_setup(wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
	}

	if (err) {
		LOG_ERR("watchdog setup failed: %d", err);
		wdt_chan = -1;
		return err;
	}

	LOG_INF("watchdog armed, %u ms CPU-time window", WDT_WINDOW_MS);
	return 0;
#else
	ARG_UNUSED(cfg);
	ARG_UNUSED(err);
	LOG_WRN("no watchdog0 alias - continuing without a watchdog");
	return 0;
#endif
}

void smart_tag_watchdog_feed(void)
{
	if (wdt_dev == NULL || wdt_chan < 0) {
		return;
	}

	(void)wdt_feed(wdt_dev, wdt_chan);
}
