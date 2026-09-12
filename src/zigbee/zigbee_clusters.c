#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "zigbee_clusters.h"

LOG_MODULE_REGISTER(zigbee_clusters, LOG_LEVEL_INF);

#ifdef CONFIG_ZIGBEE_ADD_ON

#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zb_mem_config_med.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_error_handler.h>
#include <zb_nrf_platform.h>

#include "zigbee_tag_cluster.h"
#include "zigbee_reporting.h"
#include "app/app_state.h"

#define BASIC_APP_VERSION	1
#define BASIC_STACK_VERSION	23
#define BASIC_HW_VERSION	SMART_TAG_HW_VERSION
#define BASIC_MANUF_NAME	"HOLyiot"
#define BASIC_MODEL_ID		SMART_TAG_MODEL_NAME
#define BASIC_DATE_CODE		"20260101"
#define BASIC_LOCATION		""

/* Battery quantities are in units of 100 mV, per the Power Config cluster. */
#define BATTERY_RATED_DECIVOLTS	30	/* CR2032, 3.0 V */
#define BATTERY_SIZE_COIN	0x0a	/* ZCL BatterySize: "other" coin cell */

struct power_config_attrs {
	uint8_t voltage;
	uint8_t size;
	uint8_t quantity;
	uint8_t rated_voltage;
	uint8_t alarm_mask;
	uint8_t voltage_min_threshold;
	uint8_t remaining;
	uint8_t threshold1;
	uint8_t threshold2;
	uint8_t threshold3;
	uint8_t min_threshold;
	uint8_t percent_threshold1;
	uint8_t percent_threshold2;
	uint8_t percent_threshold3;
	uint32_t alarm_state;
};

struct measure_attrs {
	int16_t temp_value;
	int16_t temp_min;
	int16_t temp_max;
	uint16_t temp_tolerance;

	uint16_t humidity_value;
	uint16_t humidity_min;
	uint16_t humidity_max;

	int16_t pressure_value;
	int16_t pressure_min;
	int16_t pressure_max;
	uint16_t pressure_tolerance;
};

struct device_ctx {
	zb_zcl_basic_attrs_ext_t basic;
	zb_zcl_identify_attrs_t identify;
	struct power_config_attrs power;
	struct measure_attrs measure;
	struct tag_monitor_attrs tag;
};

static struct device_ctx dev_ctx;

ZB_ZCL_DECLARE_BASIC_ATTRIB_LIST_EXT(
	basic_attr_list, &dev_ctx.basic.zcl_version, &dev_ctx.basic.app_version,
	&dev_ctx.basic.stack_version, &dev_ctx.basic.hw_version,
	dev_ctx.basic.mf_name, dev_ctx.basic.model_id, dev_ctx.basic.date_code,
	&dev_ctx.basic.power_source, dev_ctx.basic.location_id,
	&dev_ctx.basic.ph_env, dev_ctx.basic.sw_ver);

ZB_ZCL_DECLARE_IDENTIFY_ATTRIB_LIST(identify_attr_list,
				    &dev_ctx.identify.identify_time);

ZB_ZCL_DECLARE_POWER_CONFIG_BATTERY_ATTRIB_LIST_EXT(
	power_config_attr_list, &dev_ctx.power.voltage, &dev_ctx.power.size,
	&dev_ctx.power.quantity, &dev_ctx.power.rated_voltage,
	&dev_ctx.power.alarm_mask, &dev_ctx.power.voltage_min_threshold,
	&dev_ctx.power.remaining, &dev_ctx.power.threshold1,
	&dev_ctx.power.threshold2, &dev_ctx.power.threshold3,
	&dev_ctx.power.min_threshold, &dev_ctx.power.percent_threshold1,
	&dev_ctx.power.percent_threshold2, &dev_ctx.power.percent_threshold3,
	&dev_ctx.power.alarm_state);

ZB_ZCL_DECLARE_TEMP_MEASUREMENT_ATTRIB_LIST(
	temp_attr_list, &dev_ctx.measure.temp_value, &dev_ctx.measure.temp_min,
	&dev_ctx.measure.temp_max, &dev_ctx.measure.temp_tolerance);

ZB_ZCL_DECLARE_REL_HUMIDITY_MEASUREMENT_ATTRIB_LIST(
	humidity_attr_list, &dev_ctx.measure.humidity_value,
	&dev_ctx.measure.humidity_min, &dev_ctx.measure.humidity_max);

ZB_ZCL_DECLARE_PRESSURE_MEASUREMENT_ATTRIB_LIST(
	pressure_attr_list, &dev_ctx.measure.pressure_value,
	&dev_ctx.measure.pressure_min, &dev_ctx.measure.pressure_max,
	&dev_ctx.measure.pressure_tolerance);

ZB_ZCL_DECLARE_TAG_MONITOR_ATTRIB_LIST(tag_monitor_attr_list, &dev_ctx.tag);

ZB_DECLARE_SMART_TAG_CLUSTER_LIST(smart_tag_clusters, basic_attr_list,
				  identify_attr_list, power_config_attr_list,
				  temp_attr_list, humidity_attr_list,
				  pressure_attr_list, tag_monitor_attr_list);

ZB_DECLARE_SMART_TAG_EP(smart_tag_ep, SMART_TAG_ENDPOINT, smart_tag_clusters);

#ifdef CONFIG_ZIGBEE_FOTA
/* The FOTA library declares and owns the OTA client endpoint. */
extern zb_af_endpoint_desc_t zigbee_fota_client_ep;
ZBOSS_DECLARE_DEVICE_CTX_2_EP(smart_tag_zb_ctx, zigbee_fota_client_ep,
			      smart_tag_ep);
#else
ZBOSS_DECLARE_DEVICE_CTX_1_EP(smart_tag_zb_ctx, smart_tag_ep);
#endif

static void set_attr(zb_uint16_t cluster_id, zb_uint16_t attr_id, void *value)
{
	(void)zb_zcl_set_attr_val(SMART_TAG_ENDPOINT, cluster_id,
				  ZB_ZCL_CLUSTER_SERVER_ROLE, attr_id,
				  (zb_uint8_t *)value, ZB_FALSE);
}

static void set_tag_attr(zb_uint16_t attr_id, void *value)
{
	(void)zb_zcl_set_attr_val_manuf(SMART_TAG_ENDPOINT,
					ZB_ZCL_CLUSTER_ID_TAG_MONITOR,
					ZB_ZCL_CLUSTER_SERVER_ROLE, attr_id,
					SMART_TAG_MANUF_CODE,
					(zb_uint8_t *)value, ZB_FALSE);
}

void zigbee_clusters_register(void)
{
	ZB_AF_REGISTER_DEVICE_CTX(&smart_tag_zb_ctx);
}

void zigbee_clusters_init(const tag_config_t *config)
{
	dev_ctx.basic.zcl_version = ZB_ZCL_VERSION;
	dev_ctx.basic.app_version = BASIC_APP_VERSION;
	dev_ctx.basic.stack_version = BASIC_STACK_VERSION;
	dev_ctx.basic.hw_version = BASIC_HW_VERSION;
	dev_ctx.basic.power_source = ZB_ZCL_BASIC_POWER_SOURCE_BATTERY;
	dev_ctx.basic.ph_env = ZB_ZCL_BASIC_ENV_UNSPECIFIED;

	/* ZCL strings are length-prefixed, not NUL terminated. */
	ZB_ZCL_SET_STRING_VAL(dev_ctx.basic.mf_name, BASIC_MANUF_NAME,
			      ZB_ZCL_STRING_CONST_SIZE(BASIC_MANUF_NAME));
	ZB_ZCL_SET_STRING_VAL(dev_ctx.basic.model_id, BASIC_MODEL_ID,
			      ZB_ZCL_STRING_CONST_SIZE(BASIC_MODEL_ID));
	ZB_ZCL_SET_STRING_VAL(dev_ctx.basic.date_code, BASIC_DATE_CODE,
			      ZB_ZCL_STRING_CONST_SIZE(BASIC_DATE_CODE));
	ZB_ZCL_SET_STRING_VAL(dev_ctx.basic.location_id, BASIC_LOCATION,
			      ZB_ZCL_STRING_CONST_SIZE(BASIC_LOCATION));

	dev_ctx.identify.identify_time =
		ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;

	dev_ctx.power.size = BATTERY_SIZE_COIN;
	dev_ctx.power.quantity = 1;
	dev_ctx.power.rated_voltage = BATTERY_RATED_DECIVOLTS;
	dev_ctx.power.voltage_min_threshold =
		(uint8_t)(config->battery_low_mv / 100U);
	dev_ctx.power.voltage = 0xff;	/* unknown until the first sample */
	dev_ctx.power.remaining = 0xff;

	/*
	 * Sensor operating envelopes, in ZCL units. These describe what the
	 * fitted sensor can actually measure, so a gateway can tell a reading
	 * at the rail from a reading out of range.
	 *
	 * The upper bound is 85, not the 125 it was: the SHT40 on the HOLyiot
	 * board reaches 125 C, but the BME688 is specified to 85 C, and the
	 * BME688 is the narrower of the two. Declaring 125 on a board that
	 * cannot reach it would promise a gateway range the hardware has not
	 * got.
	 */
	dev_ctx.measure.temp_min = -4000;	/* -40.00 C */
#if DT_NODE_EXISTS(DT_ALIAS(bme688))
	dev_ctx.measure.temp_max = 8500;	/* 85.00 C, BME688 */
#else
	dev_ctx.measure.temp_max = 12500;	/* 125.00 C, SHT40 */
#endif
	dev_ctx.measure.humidity_min = 0;
	dev_ctx.measure.humidity_max = 10000;	/* 100.00 %RH */
	dev_ctx.measure.pressure_min = 26;	/* 260 hPa, reported in kPa */
	dev_ctx.measure.pressure_max = 126;	/* 1260 hPa */

	dev_ctx.tag.orientation = ORIENTATION_UNKNOWN;
	dev_ctx.tag.operating_mode = config->operating_mode;
	dev_ctx.tag.sampling_interval = config->sampling_interval_s;
}

void zigbee_clusters_update(const sensor_data_t *data,
			    const tag_status_t *status,
			    uint32_t log_record_count,
			    const tag_config_t *config, uint32_t report)
{
	int16_t value16;
	uint16_t uvalue16;

	if ((report & REPORT_TEMPERATURE) &&
	    (data->valid & SENSOR_VALID_TEMP_RH)) {
		/* ZCL MeasuredValue is 0.01 degrees C - the same unit. */
		value16 = data->temperature_c_x100;
		set_attr(ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
			 ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &value16);
	}

	if ((report & REPORT_HUMIDITY) &&
	    (data->valid & SENSOR_VALID_TEMP_RH)) {
		/* ZCL humidity is also 0.01 %RH. */
		uvalue16 = data->humidity_x100;
		set_attr(ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
			 ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID,
			 &uvalue16);
	}

	if ((report & REPORT_PRESSURE) &&
	    (data->valid & SENSOR_VALID_PRESSURE)) {
		/* ZCL pressure MeasuredValue is in kPa. */
		value16 = (int16_t)(data->pressure_pa / 1000);
		set_attr(ZB_ZCL_CLUSTER_ID_PRESSURE_MEASUREMENT,
			 ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_VALUE_ID, &value16);
	}

	if ((report & REPORT_BATTERY) && (data->valid & SENSOR_VALID_BATTERY)) {
		/* BatteryVoltage is in 100 mV units. */
		dev_ctx.power.voltage = (uint8_t)(data->battery_mv / 100U);
		set_attr(ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
			 ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID,
			 &dev_ctx.power.voltage);

		/* BatteryPercentageRemaining is in half-percent units. */
		dev_ctx.power.remaining =
			(uint8_t)MIN((uint16_t)data->battery_percent * 2U, 200U);
		set_attr(ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
			 ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
			 &dev_ctx.power.remaining);
	}

	if ((report & REPORT_TAG_MONITOR) == 0U) {
		return;
	}

	dev_ctx.tag.motion_state = status->motion_state;
	set_tag_attr(TAG_ATTR_MOTION_STATE, &dev_ctx.tag.motion_state);

	dev_ctx.tag.orientation = status->orientation;
	set_tag_attr(TAG_ATTR_ORIENTATION, &dev_ctx.tag.orientation);

	dev_ctx.tag.shock_count = status->shock_count;
	set_tag_attr(TAG_ATTR_SHOCK_COUNT, &dev_ctx.tag.shock_count);

	dev_ctx.tag.maximum_shock = status->max_shock_mg;
	set_tag_attr(TAG_ATTR_MAXIMUM_SHOCK, &dev_ctx.tag.maximum_shock);

	dev_ctx.tag.free_fall_count = status->free_fall_count;
	set_tag_attr(TAG_ATTR_FREE_FALL_COUNT, &dev_ctx.tag.free_fall_count);

	dev_ctx.tag.tamper_state = status->tamper;
	set_tag_attr(TAG_ATTR_TAMPER_STATE, &dev_ctx.tag.tamper_state);

	dev_ctx.tag.movement_duration = status->movement_duration_s;
	set_tag_attr(TAG_ATTR_MOVEMENT_DURATION,
		     &dev_ctx.tag.movement_duration);

	dev_ctx.tag.last_movement_time = status->last_movement_time;
	set_tag_attr(TAG_ATTR_LAST_MOVEMENT_TIME,
		     &dev_ctx.tag.last_movement_time);

	dev_ctx.tag.event_count = status->event_count;
	set_tag_attr(TAG_ATTR_EVENT_COUNT, &dev_ctx.tag.event_count);

	dev_ctx.tag.log_record_count = log_record_count;
	set_tag_attr(TAG_ATTR_LOG_RECORD_COUNT, &dev_ctx.tag.log_record_count);

	dev_ctx.tag.alarm_flags = status->alarm_flags;
	set_tag_attr(TAG_ATTR_ALARM_FLAGS, &dev_ctx.tag.alarm_flags);

	dev_ctx.tag.operating_mode = config->operating_mode;
	set_tag_attr(TAG_ATTR_OPERATING_MODE, &dev_ctx.tag.operating_mode);

	dev_ctx.tag.sampling_interval = config->sampling_interval_s;
	set_tag_attr(TAG_ATTR_SAMPLING_INTERVAL,
		     &dev_ctx.tag.sampling_interval);

	/*
	 * Gas resistance, guarded on the validity bit rather than on the
	 * board: on a board with no BME688 the bit is never set, so the
	 * attribute stays at its initial zero and a gateway can tell "not
	 * fitted" from a genuine reading.
	 */
	if ((data->valid & SENSOR_VALID_GAS) != 0U) {
		dev_ctx.tag.gas_resistance = data->gas_resistance_ohm;
		set_tag_attr(TAG_ATTR_GAS_RESISTANCE,
			     &dev_ctx.tag.gas_resistance);
	}

	{
		tag_statistics_t statistics;

		app_state_get_statistics(&statistics);
		dev_ctx.tag.boot_epoch = statistics.boot_epoch_utc;
		set_tag_attr(TAG_ATTR_BOOT_EPOCH, &dev_ctx.tag.boot_epoch);
	}
}

#else /* !CONFIG_ZIGBEE_ADD_ON */

void zigbee_clusters_register(void)
{
}

void zigbee_clusters_init(const tag_config_t *config)
{
	ARG_UNUSED(config);
}

void zigbee_clusters_update(const sensor_data_t *data,
			    const tag_status_t *status,
			    uint32_t log_record_count,
			    const tag_config_t *config, uint32_t report)
{
	ARG_UNUSED(data);
	ARG_UNUSED(status);
	ARG_UNUSED(log_record_count);
	ARG_UNUSED(config);
	ARG_UNUSED(report);
}

#endif /* CONFIG_ZIGBEE_ADD_ON */
