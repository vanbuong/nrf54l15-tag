/*
 * Zigbee stack lifecycle - PLAN.md sections 5, 6 and 11.
 *
 * Owns commissioning, the sleepy end device configuration, the signal handler
 * and the ZCL device callback. The data model lives in zigbee_clusters.c and
 * the report-or-not decision in zigbee_reporting.c.
 *
 * Needs the Nordic Zigbee R23 add-on (nrfconnect/ncs-zigbee) in the
 * workspace. Without CONFIG_ZIGBEE_ADD_ON the implementation is compiled out
 * and zigbee_manager_init() returns -ENOTSUP.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "zigbee_manager.h"

LOG_MODULE_REGISTER(zigbee_manager, LOG_LEVEL_INF);

#ifndef CONFIG_ZIGBEE_ADD_ON

int zigbee_manager_init(void)
{
	LOG_INF("Zigbee is not built in (CONFIG_ZIGBEE_ADD_ON=n)");
	return -ENOTSUP;
}

bool zigbee_manager_is_joined(void)
{
	return false;
}

void zigbee_manager_factory_reset(void)
{
}

#else /* CONFIG_ZIGBEE_ADD_ON */

#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zb_mem_config_med.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_error_handler.h>
#include <zb_nrf_platform.h>

#include "zigbee_clusters.h"
#include "zigbee_reporting.h"
#include "zigbee_tag_cluster.h"
#include "zigbee_ota.h"
#include "../app/app_state.h"
#include "../app/app_config.h"
#include "../storage/event_log.h"
#include "../ui/led_manager.h"
#include "../power/power_manager.h"

/*
 * Sleepy end device polling. The tag wakes on its own sampling interval, so
 * a long poll interval only affects how quickly downlink commands arrive.
 */
#define LONG_POLL_INTERVAL_MS	15000

#define IDENTIFY_DURATION_S	10

static bool joined;
static bool joining;

/* --------------------------------------------------------------------------
 * Publishing, driven by the application subscriber
 * ------------------------------------------------------------------------ */

/* Runs on the ZBOSS thread; ZCL writes are not safe from other threads. */
static void publish_attributes(zb_bufid_t bufid)
{
	struct flash_log_info log_info;
	sensor_data_t data;
	tag_status_t status;
	tag_config_t config;
	uint32_t report;

	ZVUNUSED(bufid);

	if (!joined) {
		return;
	}

	app_state_get_last_sample(&data);
	app_state_get_status(&status);
	app_config_get(&config);
	event_log_get_info(&log_info);

	report = zigbee_reporting_evaluate(&data, &status, &config);

	if (report == 0U) {
		LOG_DBG("nothing changed enough to report");
		return;
	}

	zigbee_clusters_update(&data, &status, log_info.record_count, &config,
			       report);
	zigbee_reporting_commit(&data, &status, report);
}

static void on_sensor_data(const sensor_data_t *data, void *user_data)
{
	ARG_UNUSED(data);
	ARG_UNUSED(user_data);

	if (!joined) {
		return;
	}

	ZB_SCHEDULE_APP_CALLBACK(publish_attributes, 0);
}

static void on_event(const event_record_t *event, void *user_data)
{
	ARG_UNUSED(user_data);

	if (!joined) {
		return;
	}

	/*
	 * Events surface through the Tag Monitor counters and alarm flags, and
	 * PLAN.md section 6 wants motion, shock, free fall and tamper reported
	 * immediately. The reporting policy will see the changed counters and
	 * let them through.
	 */
	LOG_DBG("event %u: refreshing Zigbee attributes", event->type);
	ZB_SCHEDULE_APP_CALLBACK(publish_attributes, 0);
}

static struct app_subscriber zb_subscriber = {
	.on_sensor_data = on_sensor_data,
	.on_event = on_event,
};

/* --------------------------------------------------------------------------
 * Identify
 * ------------------------------------------------------------------------ */

static void identify_cb(zb_bufid_t bufid)
{
	if (bufid) {
		led_manager_identify(IDENTIFY_DURATION_S);
		zb_buf_free(bufid);
	} else {
		led_manager_set(LED_STATE_IDENTIFY, false);
	}
}

/* --------------------------------------------------------------------------
 * ZCL device callback: attribute writes from the coordinator
 * ------------------------------------------------------------------------ */

static void zcl_device_cb(zb_bufid_t bufid)
{
	zb_zcl_device_callback_param_t *param =
		ZB_BUF_GET_PARAM(bufid, zb_zcl_device_callback_param_t);
	tag_config_t config;
	uint16_t cluster_id;
	uint16_t attr_id;

	param->status = RET_OK;

	switch (param->device_cb_id) {
	case ZB_ZCL_SET_ATTR_VALUE_CB_ID:
		cluster_id = param->cb_param.set_attr_value_param.cluster_id;
		attr_id = param->cb_param.set_attr_value_param.attr_id;

		if (cluster_id != ZB_ZCL_CLUSTER_ID_TAG_MONITOR) {
			param->status = RET_NOT_IMPLEMENTED;
			break;
		}

		app_config_get(&config);

		if (attr_id == TAG_ATTR_OPERATING_MODE) {
			config.operating_mode =
				param->cb_param.set_attr_value_param.values.data8;
		} else if (attr_id == TAG_ATTR_SAMPLING_INTERVAL) {
			config.sampling_interval_s =
				param->cb_param.set_attr_value_param.values.data16;
		} else {
			param->status = RET_NOT_IMPLEMENTED;
			break;
		}

		if (app_config_set(&config) != 0) {
			param->status = RET_INVALID_PARAMETER;
			break;
		}

		power_manager_config_changed();
		(void)app_raise_event(EVENT_CONFIG_CHANGED,
				      EVENT_SEVERITY_INFO,
				      (int16_t)config.operating_mode);
		break;

	default:
		if (!zigbee_ota_zcl_cb(bufid)) {
			param->status = RET_NOT_IMPLEMENTED;
		}
		break;
	}
}

/* --------------------------------------------------------------------------
 * Stack signals
 * ------------------------------------------------------------------------ */

void zboss_signal_handler(zb_bufid_t bufid)
{
	zb_zdo_app_signal_hdr_t *header = NULL;
	zb_zdo_app_signal_type_t signal = zb_get_app_signal(bufid, &header);
	zb_ret_t status = ZB_GET_APP_SIGNAL_STATUS(bufid);

	zigbee_ota_signal_handler(bufid);

	switch (signal) {
	case ZB_BDB_SIGNAL_DEVICE_FIRST_START:
	case ZB_BDB_SIGNAL_DEVICE_REBOOT:
	case ZB_BDB_SIGNAL_STEERING:
		if (status == RET_OK) {
			if (!joined) {
				LOG_INF("joined a Zigbee network");
				(void)app_raise_event(EVENT_ZIGBEE_JOINED,
						      EVENT_SEVERITY_INFO, 0);
			}

			joined = true;
			joining = false;
			led_manager_set(LED_STATE_ZIGBEE_JOINING, false);
			led_manager_set(LED_STATE_ZIGBEE_CONNECTED, true);

			/* Send a full snapshot as soon as we are up. */
			zigbee_reporting_force();
			ZB_SCHEDULE_APP_CALLBACK(publish_attributes, 0);
		} else {
			LOG_WRN("commissioning failed: %d", (int)status);
			joining = true;
			led_manager_set(LED_STATE_ZIGBEE_JOINING, true);
			led_manager_set(LED_STATE_ZIGBEE_CONNECTED, false);
		}
		break;

	case ZB_ZDO_SIGNAL_LEAVE:
		LOG_WRN("left the Zigbee network");
		joined = false;
		joining = true;
		led_manager_set(LED_STATE_ZIGBEE_CONNECTED, false);
		led_manager_set(LED_STATE_ZIGBEE_JOINING, true);
		(void)app_raise_event(EVENT_ZIGBEE_LEFT,
				      EVENT_SEVERITY_WARNING, 0);
		break;

	default:
		break;
	}

	ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));

	if (bufid) {
		zb_buf_free(bufid);
	}
}

/* --------------------------------------------------------------------------
 * Init
 * ------------------------------------------------------------------------ */

bool zigbee_manager_is_joined(void)
{
	return joined;
}

void zigbee_manager_factory_reset(void)
{
	zigbee_erase_persistent_storage(ZB_TRUE);
}

int zigbee_manager_init(void)
{
	tag_config_t config;

	app_config_get(&config);

	ZB_ZCL_REGISTER_DEVICE_CB(zcl_device_cb);
	zigbee_clusters_register();
	ZB_AF_SET_IDENTIFY_NOTIFICATION_HANDLER(SMART_TAG_ENDPOINT, identify_cb);

	zigbee_clusters_init(&config);
	zigbee_reporting_init();

	if (IS_ENABLED(CONFIG_ZIGBEE_FOTA)) {
		(void)zigbee_ota_init();
	}

	/*
	 * Sleepy end device: the radio is off between polls, which is what
	 * makes the coin-cell budget work.
	 */
	zigbee_configure_sleepy_behavior(true);
	zb_set_rx_on_when_idle(ZB_FALSE);
	zb_zdo_pim_set_long_poll_interval(LONG_POLL_INTERVAL_MS);

	app_state_subscribe(&zb_subscriber);

	joining = true;
	led_manager_set(LED_STATE_ZIGBEE_JOINING, true);

	zigbee_enable();

	LOG_INF("Zigbee sleepy end device starting on endpoint %u",
		SMART_TAG_ENDPOINT);

	return 0;
}

#endif /* CONFIG_ZIGBEE_ADD_ON */
