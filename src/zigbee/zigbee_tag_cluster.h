/*
 * Tag Monitor Cluster and Smart Tag device definition - PLAN.md section 5.
 *
 * One primary endpoint carrying the standard measurement clusters plus a
 * manufacturer-specific cluster for the state that has no standard ZCL home.
 * Attribute IDs are exactly the list in PLAN.md section 5, so an existing
 * gateway's generic cluster parser can consume them unchanged.
 *
 * Endpoint 1
 *   Basic, Identify, Power Configuration,
 *   Temperature Measurement, Relative Humidity Measurement,
 *   Pressure Measurement, Tag Monitor Cluster
 *
 * OTA Upgrade lives on its own endpoint, declared and owned by the add-on's
 * zigbee_fota library (CONFIG_ZIGBEE_FOTA_ENDPOINT, default 10).
 */
#ifndef ZIGBEE_TAG_CLUSTER_H
#define ZIGBEE_TAG_CLUSTER_H

#include <stdint.h>

#define SMART_TAG_ENDPOINT		1

/*
 * Reported as an HA Temperature Sensor so generic gateways recognise the
 * device; the extra clusters are discovered from the simple descriptor.
 */
#define SMART_TAG_DEVICE_ID		0x0302
#define SMART_TAG_DEVICE_VERSION	1

#define SMART_TAG_IN_CLUSTER_NUM	7
#define SMART_TAG_OUT_CLUSTER_NUM	1

/*
 * Reportable attributes: temperature, humidity, pressure and battery
 * percentage, plus the eight reportable Tag Monitor attributes (motion state,
 * orientation, shock count, maximum shock, free-fall count, tamper state,
 * alarm flags and gas resistance) - twelve, with headroom.
 */
#define SMART_TAG_REPORT_ATTR_COUNT	16

/*
 * TODO: 0x127F is Nordic's manufacturer code, used so the tree is
 * self-consistent. Replace it with the code allocated to the product before
 * shipping, or a gateway will attribute these to Nordic.
 */
#define ZB_ZCL_CLUSTER_ID_TAG_MONITOR	0xfc00
#define SMART_TAG_MANUF_CODE		0x127f

/* PLAN.md section 5 attribute map. */
#define TAG_ATTR_MOTION_STATE		0x0000	/* u8  */
#define TAG_ATTR_ORIENTATION		0x0001	/* u8  */
#define TAG_ATTR_SHOCK_COUNT		0x0002	/* u32 */
#define TAG_ATTR_MAXIMUM_SHOCK		0x0003	/* u16, milli-g */
#define TAG_ATTR_FREE_FALL_COUNT	0x0004	/* u32 */
#define TAG_ATTR_TAMPER_STATE		0x0005	/* u8  */
#define TAG_ATTR_MOVEMENT_DURATION	0x0006	/* u32, seconds */
#define TAG_ATTR_LAST_MOVEMENT_TIME	0x0007	/* u32, uptime seconds */
#define TAG_ATTR_EVENT_COUNT		0x0008	/* u32 */
#define TAG_ATTR_LOG_RECORD_COUNT	0x0009	/* u32 */
#define TAG_ATTR_ALARM_FLAGS		0x000a	/* 16-bit bitmap */
#define TAG_ATTR_OPERATING_MODE		0x000b	/* u8,  writable */
#define TAG_ATTR_SAMPLING_INTERVAL	0x000c	/* u16, writable */
/*
 * Protocol v3, nRF54L15 Tag only. The BME688's gas resistance has no
 * standard ZCL home: the closest standard clusters measure a named analyte
 * (Carbon Dioxide, Formaldehyde, ...) in parts-per-million, whereas this is
 * an uncalibrated whole-VOC resistance in ohms. Publishing it as one of
 * those would misdescribe it to every gateway that understands them, so it
 * lives here with the rest of the manufacturer-specific state.
 *
 * Zero means "no gas sensor on this board" - the HOLyiot build never writes
 * it.
 */
#define TAG_ATTR_GAS_RESISTANCE		0x000d	/* u32, ohms */
#define TAG_ATTR_BOOT_EPOCH		0x000e	/* u32, UTC at boot; write = now */

/** Attribute storage for the Tag Monitor cluster. */
struct tag_monitor_attrs {
	uint8_t motion_state;
	uint8_t orientation;
	uint32_t shock_count;
	uint16_t maximum_shock;
	uint32_t free_fall_count;
	uint8_t tamper_state;
	uint32_t movement_duration;
	uint32_t last_movement_time;
	uint32_t event_count;
	uint32_t log_record_count;
	uint16_t alarm_flags;
	uint8_t operating_mode;
	uint16_t sampling_interval;
	uint32_t gas_resistance;
	uint32_t boot_epoch;
};

#define ZB_ZCL_DECLARE_TAG_MONITOR_ATTRIB_LIST(attr_list, attrs)               \
	ZB_ZCL_START_DECLARE_ATTRIB_LIST(attr_list)                           \
	ZB_ZCL_SET_MANUF_SPEC_ATTR_DESC(                                      \
		TAG_ATTR_MOTION_STATE, ZB_ZCL_ATTR_TYPE_U8,                   \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING,  \
		SMART_TAG_MANUF_CODE, &(attrs)->motion_state)                 \
	ZB_ZCL_SET_MANUF_SPEC_ATTR_DESC(                                      \
		TAG_ATTR_ORIENTATION, ZB_ZCL_ATTR_TYPE_U8,                    \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING,  \
		SMART_TAG_MANUF_CODE, &(attrs)->orientation)                  \
	ZB_ZCL_SET_MANUF_SPEC_ATTR_DESC(                                      \
		TAG_ATTR_SHOCK_COUNT, ZB_ZCL_ATTR_TYPE_U32,                   \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING,  \
		SMART_TAG_MANUF_CODE, &(attrs)->shock_count)                  \
	ZB_ZCL_SET_MANUF_SPEC_ATTR_DESC(                                      \
		TAG_ATTR_MAXIMUM_SHOCK, ZB_ZCL_ATTR_TYPE_U16,                 \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING,  \
		SMART_TAG_MANUF_CODE, &(attrs)->maximum_shock)                \
	ZB_ZCL_SET_MANUF_SPEC_ATTR_DESC(                                      \
		TAG_ATTR_FREE_FALL_COUNT, ZB_ZCL_ATTR_TYPE_U32,               \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING,  \
		SMART_TAG_MANUF_CODE, &(attrs)->free_fall_count)              \
	ZB_ZCL_SET_MANUF_SPEC_ATTR_DESC(                                      \
		TAG_ATTR_TAMPER_STATE, ZB_ZCL_ATTR_TYPE_U8,                   \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING,  \
		SMART_TAG_MANUF_CODE, &(attrs)->tamper_state)                 \
	ZB_ZCL_SET_MANUF_SPEC_ATTR_DESC(                                      \
		TAG_ATTR_MOVEMENT_DURATION, ZB_ZCL_ATTR_TYPE_U32,             \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY,                                 \
		SMART_TAG_MANUF_CODE, &(attrs)->movement_duration)            \
	ZB_ZCL_SET_MANUF_SPEC_ATTR_DESC(                                      \
		TAG_ATTR_LAST_MOVEMENT_TIME, ZB_ZCL_ATTR_TYPE_U32,            \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY,                                 \
		SMART_TAG_MANUF_CODE, &(attrs)->last_movement_time)           \
	ZB_ZCL_SET_MANUF_SPEC_ATTR_DESC(                                      \
		TAG_ATTR_EVENT_COUNT, ZB_ZCL_ATTR_TYPE_U32,                   \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY,                                 \
		SMART_TAG_MANUF_CODE, &(attrs)->event_count)                  \
	ZB_ZCL_SET_MANUF_SPEC_ATTR_DESC(                                      \
		TAG_ATTR_LOG_RECORD_COUNT, ZB_ZCL_ATTR_TYPE_U32,              \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY,                                 \
		SMART_TAG_MANUF_CODE, &(attrs)->log_record_count)             \
	ZB_ZCL_SET_MANUF_SPEC_ATTR_DESC(                                      \
		TAG_ATTR_ALARM_FLAGS, ZB_ZCL_ATTR_TYPE_16BITMAP,              \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING,  \
		SMART_TAG_MANUF_CODE, &(attrs)->alarm_flags)                  \
	ZB_ZCL_SET_MANUF_SPEC_ATTR_DESC(                                      \
		TAG_ATTR_OPERATING_MODE, ZB_ZCL_ATTR_TYPE_U8,                 \
		ZB_ZCL_ATTR_ACCESS_READ_WRITE,                                \
		SMART_TAG_MANUF_CODE, &(attrs)->operating_mode)               \
	ZB_ZCL_SET_MANUF_SPEC_ATTR_DESC(                                      \
		TAG_ATTR_SAMPLING_INTERVAL, ZB_ZCL_ATTR_TYPE_U16,             \
		ZB_ZCL_ATTR_ACCESS_READ_WRITE,                                \
		SMART_TAG_MANUF_CODE, &(attrs)->sampling_interval)            \
	ZB_ZCL_SET_MANUF_SPEC_ATTR_DESC(                                      \
		TAG_ATTR_GAS_RESISTANCE, ZB_ZCL_ATTR_TYPE_U32,                \
		ZB_ZCL_ATTR_ACCESS_READ_ONLY | ZB_ZCL_ATTR_ACCESS_REPORTING,  \
		SMART_TAG_MANUF_CODE, &(attrs)->gas_resistance)               \
	ZB_ZCL_SET_MANUF_SPEC_ATTR_DESC(                                      \
		TAG_ATTR_BOOT_EPOCH, ZB_ZCL_ATTR_TYPE_U32,                    \
		ZB_ZCL_ATTR_ACCESS_READ_WRITE,                                \
		SMART_TAG_MANUF_CODE, &(attrs)->boot_epoch)                   \
	ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST

#define ZB_DECLARE_SMART_TAG_CLUSTER_LIST(cluster_list_name,                  \
					  basic_attr_list,                    \
					  identify_attr_list,                 \
					  power_config_attr_list,             \
					  temp_attr_list,                     \
					  humidity_attr_list,                 \
					  pressure_attr_list,                 \
					  tag_monitor_attr_list)              \
	zb_zcl_cluster_desc_t cluster_list_name[] = {                         \
		ZB_ZCL_CLUSTER_DESC(                                          \
			ZB_ZCL_CLUSTER_ID_BASIC,                              \
			ZB_ZCL_ARRAY_SIZE(basic_attr_list, zb_zcl_attr_t),    \
			(basic_attr_list), ZB_ZCL_CLUSTER_SERVER_ROLE,        \
			ZB_ZCL_MANUF_CODE_INVALID),                           \
		ZB_ZCL_CLUSTER_DESC(                                          \
			ZB_ZCL_CLUSTER_ID_IDENTIFY,                           \
			ZB_ZCL_ARRAY_SIZE(identify_attr_list, zb_zcl_attr_t), \
			(identify_attr_list), ZB_ZCL_CLUSTER_SERVER_ROLE,     \
			ZB_ZCL_MANUF_CODE_INVALID),                           \
		ZB_ZCL_CLUSTER_DESC(                                          \
			ZB_ZCL_CLUSTER_ID_POWER_CONFIG,                       \
			ZB_ZCL_ARRAY_SIZE(power_config_attr_list,             \
					  zb_zcl_attr_t),                     \
			(power_config_attr_list),                             \
			ZB_ZCL_CLUSTER_SERVER_ROLE,                           \
			ZB_ZCL_MANUF_CODE_INVALID),                           \
		ZB_ZCL_CLUSTER_DESC(                                          \
			ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,                   \
			ZB_ZCL_ARRAY_SIZE(temp_attr_list, zb_zcl_attr_t),     \
			(temp_attr_list), ZB_ZCL_CLUSTER_SERVER_ROLE,         \
			ZB_ZCL_MANUF_CODE_INVALID),                           \
		ZB_ZCL_CLUSTER_DESC(                                          \
			ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,           \
			ZB_ZCL_ARRAY_SIZE(humidity_attr_list, zb_zcl_attr_t), \
			(humidity_attr_list), ZB_ZCL_CLUSTER_SERVER_ROLE,     \
			ZB_ZCL_MANUF_CODE_INVALID),                           \
		ZB_ZCL_CLUSTER_DESC(                                          \
			ZB_ZCL_CLUSTER_ID_PRESSURE_MEASUREMENT,               \
			ZB_ZCL_ARRAY_SIZE(pressure_attr_list, zb_zcl_attr_t), \
			(pressure_attr_list), ZB_ZCL_CLUSTER_SERVER_ROLE,     \
			ZB_ZCL_MANUF_CODE_INVALID),                           \
		ZB_ZCL_CLUSTER_DESC(                                          \
			ZB_ZCL_CLUSTER_ID_TAG_MONITOR,                        \
			ZB_ZCL_ARRAY_SIZE(tag_monitor_attr_list,              \
					  zb_zcl_attr_t),                     \
			(tag_monitor_attr_list),                              \
			ZB_ZCL_CLUSTER_SERVER_ROLE, SMART_TAG_MANUF_CODE),    \
		ZB_ZCL_CLUSTER_DESC(                                          \
			ZB_ZCL_CLUSTER_ID_IDENTIFY, 0, NULL,                  \
			ZB_ZCL_CLUSTER_CLIENT_ROLE,                           \
			ZB_ZCL_MANUF_CODE_INVALID),                           \
	}

#define ZB_ZCL_DECLARE_SMART_TAG_SIMPLE_DESC(ep_name, ep_id, in_clust_num,    \
					     out_clust_num)                   \
	ZB_DECLARE_SIMPLE_DESC(in_clust_num, out_clust_num);                  \
	ZB_AF_SIMPLE_DESC_TYPE(in_clust_num, out_clust_num)                   \
	simple_desc_##ep_name = {                                             \
		ep_id,                                                        \
		ZB_AF_HA_PROFILE_ID,                                          \
		SMART_TAG_DEVICE_ID,                                          \
		SMART_TAG_DEVICE_VERSION,                                     \
		0,                                                            \
		in_clust_num,                                                 \
		out_clust_num,                                                \
		{                                                             \
			ZB_ZCL_CLUSTER_ID_BASIC,                              \
			ZB_ZCL_CLUSTER_ID_IDENTIFY,                           \
			ZB_ZCL_CLUSTER_ID_POWER_CONFIG,                       \
			ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,                   \
			ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,           \
			ZB_ZCL_CLUSTER_ID_PRESSURE_MEASUREMENT,               \
			ZB_ZCL_CLUSTER_ID_TAG_MONITOR,                        \
			ZB_ZCL_CLUSTER_ID_IDENTIFY,                           \
		}                                                             \
	}

#define ZB_DECLARE_SMART_TAG_EP(ep_name, ep_id, cluster_list)                 \
	ZB_ZCL_DECLARE_SMART_TAG_SIMPLE_DESC(ep_name, ep_id,                  \
					     SMART_TAG_IN_CLUSTER_NUM,        \
					     SMART_TAG_OUT_CLUSTER_NUM);      \
	ZBOSS_DEVICE_DECLARE_REPORTING_CTX(reporting_info##ep_name,           \
					   SMART_TAG_REPORT_ATTR_COUNT);      \
	ZB_AF_DECLARE_ENDPOINT_DESC(                                          \
		ep_name, ep_id, ZB_AF_HA_PROFILE_ID, 0, NULL,                 \
		ZB_ZCL_ARRAY_SIZE(cluster_list, zb_zcl_cluster_desc_t),       \
		cluster_list,                                                 \
		(zb_af_simple_desc_1_1_t *)&simple_desc_##ep_name,            \
		SMART_TAG_REPORT_ATTR_COUNT, reporting_info##ep_name, 0, NULL)

#endif /* ZIGBEE_TAG_CLUSTER_H */
