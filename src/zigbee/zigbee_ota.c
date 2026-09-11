#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "zigbee_ota.h"

LOG_MODULE_REGISTER(zigbee_ota, LOG_LEVEL_INF);

#if defined(CONFIG_ZIGBEE_ADD_ON) && defined(CONFIG_ZIGBEE_FOTA)

#include <zboss_api.h>
#include <zigbee/zigbee_fota.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>

#include "../app/app_state.h"
#include "../ui/led_manager.h"

static bool in_progress;

static void fota_evt_handler(const struct zigbee_fota_evt *evt)
{
	switch (evt->id) {
	case ZIGBEE_FOTA_EVT_PROGRESS:
		if (!in_progress) {
			in_progress = true;
			led_manager_set(LED_STATE_OTA, true);
			(void)app_raise_event(EVENT_OTA_STARTED,
					      EVENT_SEVERITY_INFO, 0);
		}

		LOG_INF("OTA progress: %u%%", evt->dl.progress);
		break;

	case ZIGBEE_FOTA_EVT_FINISHED:
		LOG_INF("OTA image received, rebooting into the new image");
		(void)app_raise_event(EVENT_OTA_FINISHED, EVENT_SEVERITY_INFO,
				      0);
		app_state_flush_statistics();
		led_manager_set(LED_STATE_OTA, false);
		in_progress = false;
		k_sleep(K_MSEC(500));
		sys_reboot(SYS_REBOOT_COLD);
		break;

	case ZIGBEE_FOTA_EVT_ERROR:
		LOG_ERR("OTA transfer failed");
		led_manager_set(LED_STATE_OTA, false);
		in_progress = false;
		break;

	default:
		break;
	}
}

int zigbee_ota_init(void)
{
	int err = zigbee_fota_init(fota_evt_handler);

	if (err) {
		LOG_ERR("Zigbee FOTA init failed: %d", err);
		return err;
	}

	/*
	 * The image booted far enough to run the Zigbee stack, so tell MCUboot
	 * to stop reverting it on the next reset.
	 */
	if (!boot_is_img_confirmed()) {
		err = boot_write_img_confirmed();
		if (err) {
			LOG_ERR("cannot confirm the running image: %d", err);
		} else {
			LOG_INF("running image confirmed");
		}
	}

	return 0;
}

void zigbee_ota_signal_handler(uint32_t bufid)
{
	zigbee_fota_signal_handler((zb_bufid_t)bufid);
}

bool zigbee_ota_zcl_cb(uint32_t bufid)
{
	zigbee_fota_zcl_cb((zb_bufid_t)bufid);
	return true;
}

bool zigbee_ota_in_progress(void)
{
	return in_progress;
}

#else /* OTA not built in */

int zigbee_ota_init(void)
{
	return -ENOTSUP;
}

void zigbee_ota_signal_handler(uint32_t bufid)
{
	ARG_UNUSED(bufid);
}

bool zigbee_ota_zcl_cb(uint32_t bufid)
{
	ARG_UNUSED(bufid);
	return false;
}

bool zigbee_ota_in_progress(void)
{
	return false;
}

#endif
