/*
 * Smart Tag shared types.
 *
 * Wire structs and constants only. This header is deliberately free of
 * Zephyr includes so the same types compile in host Unity tests
 * (doc/DESIGN.md §10, NFR-M1). Board identity strings live in board_id.h
 * and are included by firmware sources that need them.
 */
#ifndef SMART_TAG_H
#define SMART_TAG_H

#include <stdbool.h>
#include <stdint.h>

#ifndef BIT
#define BIT(n) (1u << (n))
#endif

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

/*
 * Bumped whenever a wire struct below changes shape.
 *
 * v3: the nRF54L15 Tag port. sensor_data_t gained gas resistance (BME688)
 *     and a gyroscope (BMI270), sensor_log_record_t gained gas, and
 *     sensor_data_t became __packed - it was the one wire struct that was
 *     not, which left a compiler padding hole a decoder had to know about.
 */
#define SMART_TAG_PROTOCOL_VERSION 3

#define SMART_TAG_HW_VERSION	1

#ifndef SMART_TAG_MODEL_NAME
#define SMART_TAG_MODEL_NAME	"Smart Tag"
#endif

/* 1 g in the milli-g units used throughout the application. */
#define SMART_TAG_ONE_G_MG		1000

/* CR2032 under a light pulsed load. Linear map, not a chemistry model. */
#define SMART_TAG_BATTERY_FULL_MV	3000
#define SMART_TAG_BATTERY_EMPTY_MV	2000
#define SMART_TAG_BATTERY_HYSTERESIS_MV	100

/*
 * PLAN.md section 14 / DESIGN.md section 2: one product, one firmware,
 * several roles. The mode selects sampling emphasis and reporting policy,
 * not a different build.
 */
typedef enum {
	TAG_MODE_ENVIRONMENT = 0,	/* slow climate logging, motion secondary */
	TAG_MODE_ASSET = 1,		/* presence and tamper matter most */
	TAG_MODE_SHIPPING = 2,		/* shock and free fall matter most */
} tag_mode_t;

typedef enum {
	MOTION_STATE_STATIONARY = 0,
	MOTION_STATE_MOVING = 1,
} motion_state_t;

typedef enum {
	ORIENTATION_UNKNOWN = 0,
	ORIENTATION_X_UP,
	ORIENTATION_X_DOWN,
	ORIENTATION_Y_UP,
	ORIENTATION_Y_DOWN,
	ORIENTATION_Z_UP,
	ORIENTATION_Z_DOWN,
} orientation_t;

/* Event types, stored in event_record_t.type. Append-only (PROTO-3). */
typedef enum {
	EVENT_BOOT = 0,
	EVENT_MOTION_START,
	EVENT_MOTION_STOP,
	EVENT_SHOCK,
	EVENT_FREE_FALL,
	EVENT_ORIENTATION_CHANGE,
	EVENT_TAMPER,
	EVENT_TEMP_HIGH,
	EVENT_TEMP_LOW,
	EVENT_HUMIDITY_HIGH,
	EVENT_BATTERY_LOW,
	EVENT_CONFIG_CHANGED,
	EVENT_SENSOR_FAULT,
	EVENT_ZIGBEE_JOINED,
	EVENT_ZIGBEE_LEFT,
	EVENT_BLE_CONNECTED,
	EVENT_OTA_STARTED,
	EVENT_OTA_FINISHED,
	EVENT_FACTORY_RESET,
	EVENT_GAS_LOW,
} event_type_t;

typedef enum {
	EVENT_SEVERITY_INFO = 0,
	EVENT_SEVERITY_WARNING = 1,
	EVENT_SEVERITY_ALARM = 2,
} event_severity_t;

/* Bits in tag_status_t.alarm_flags and the Zigbee AlarmFlags attribute. */
#define ALARM_TEMP_HIGH		BIT(0)
#define ALARM_TEMP_LOW		BIT(1)
#define ALARM_HUMIDITY_HIGH	BIT(2)
#define ALARM_SHOCK		BIT(3)
#define ALARM_FREE_FALL		BIT(4)
#define ALARM_TAMPER		BIT(5)
#define ALARM_SENSOR_FAULT	BIT(6)
#define ALARM_BATTERY_LOW	BIT(7)
#define ALARM_GAS_LOW		BIT(8)

/*
 * Bits in sensor_data_t.valid and sensor_log_record_t.flags.
 *
 * This byte is shared between the live struct and the flash record, so the
 * two sets below are one namespace. Bits 4 and 5 were the last free ones and
 * are now spent; a further sensor needs a wider field and another protocol
 * bump.
 */
#define SENSOR_VALID_TEMP_RH	BIT(0)
#define SENSOR_VALID_PRESSURE	BIT(1)
#define SENSOR_VALID_ACCEL	BIT(2)
#define SENSOR_VALID_BATTERY	BIT(3)
#define SENSOR_VALID_GAS	BIT(4)
#define SENSOR_VALID_GYRO	BIT(5)
#define SENSOR_FLAG_MOVING	BIT(6)
#define SENSOR_FLAG_ALARM	BIT(7)

/**
 * The common sensor structure, extended with battery (V1 requirement),
 * gas/gyro (protocol v3) and a timestamp so a reading can be logged without
 * a second lookup.
 */
typedef struct {
	uint32_t timestamp;		/* seconds since boot */

	int16_t temperature_c_x100;
	uint16_t humidity_x100;
	int32_t pressure_pa;

	uint16_t battery_mv;
	uint8_t battery_percent;

	int16_t accel_x_mg;
	int16_t accel_y_mg;
	int16_t accel_z_mg;
	uint16_t magnitude_mg;

	bool motion;
	bool free_fall;
	bool shock;

	uint8_t orientation;
	uint8_t valid;			/* SENSOR_VALID_* */

	uint32_t gas_resistance_ohm;	/* BME688, ohms; 0 if not fitted */
	int16_t gyro_x_dps_x10;		/* BMI270, 0.1 deg/s; live only */
	int16_t gyro_y_dps_x10;
	int16_t gyro_z_dps_x10;
} __packed sensor_data_t;

/** Live state and running counters, surfaced over both protocols. */
typedef struct {
	uint8_t motion_state;		/* motion_state_t */
	uint8_t orientation;		/* orientation_t */
	uint8_t operating_mode;		/* tag_mode_t */
	uint8_t tamper;
	uint16_t alarm_flags;		/* ALARM_* */
	uint16_t max_shock_mg;
	uint32_t shock_count;
	uint32_t free_fall_count;
	uint32_t event_count;
	uint32_t movement_duration_s;
	uint32_t last_movement_time;
} __packed tag_status_t;

/**
 * Persistent configuration. Written verbatim by the BLE Configuration
 * characteristic.
 */
typedef struct {
	uint16_t sampling_interval_s;
	uint16_t motion_timeout_s;
	uint16_t motion_threshold_mg;
	uint16_t shock_threshold_mg;
	uint16_t free_fall_threshold_mg;

	int16_t temp_high_c_x100;
	int16_t temp_low_c_x100;
	uint16_t humidity_high_x100;
	uint16_t battery_low_mv;

	uint16_t report_temp_delta_c_x100;
	uint16_t report_humidity_delta_x100;
	uint16_t report_pressure_delta_pa;
	uint16_t report_max_interval_s;

	uint16_t ble_service_window_s;

	uint32_t gas_low_threshold_ohm;
	uint32_t report_gas_delta_ohm;

	uint8_t operating_mode;		/* tag_mode_t */
	uint8_t led_enabled;
	uint8_t sensor_history_enabled;
	uint8_t reserved;
} __packed tag_config_t;

#define TAG_CONFIG_DEFAULTS {				\
	.sampling_interval_s = 60,			\
	.motion_timeout_s = 30,				\
	.motion_threshold_mg = 150,			\
	.shock_threshold_mg = 4000,			\
	.free_fall_threshold_mg = 300,			\
	.temp_high_c_x100 = 4000,			\
	.temp_low_c_x100 = -1000,			\
	.humidity_high_x100 = 8000,			\
	.battery_low_mv = 2400,				\
	.report_temp_delta_c_x100 = 50,			\
	.report_humidity_delta_x100 = 300,			\
	.report_pressure_delta_pa = 100,		\
	.report_max_interval_s = 1800,			\
	.ble_service_window_s = 120,			\
	.gas_low_threshold_ohm = 0,			\
	.report_gas_delta_ohm = 5000,			\
	.operating_mode = TAG_MODE_ENVIRONMENT,		\
	.led_enabled = 1,				\
	.sensor_history_enabled = 1,			\
	.reserved = 0,					\
}

/** Lifetime device statistics. */
typedef struct {
	uint32_t boot_count;
	uint32_t total_uptime_s;
	uint32_t motion_count;
	uint32_t shock_count;
	uint32_t free_fall_count;
	uint32_t event_count;
	uint32_t movement_duration_s;
	uint16_t max_shock_mg;
	uint16_t min_battery_mv;
	uint16_t zigbee_reports;
	uint16_t ble_connections;
	uint16_t sensor_faults;
	uint16_t reserved;
} __packed tag_statistics_t;

typedef struct {
	uint32_t timestamp;
	int16_t temperature;		/* 0.01 degrees C */
	uint16_t humidity;		/* 0.01 %RH */
	int32_t pressure;		/* Pa */
	uint32_t gas_resistance_ohm;	/* protocol v3, BME688 only */
	uint8_t flags;			/* SENSOR_VALID_* | SENSOR_FLAG_* */
} __packed sensor_log_record_t;

typedef struct {
	uint32_t timestamp;
	uint8_t type;			/* event_type_t */
	uint8_t severity;		/* event_severity_t */
	int16_t value;			/* type specific: shock mg, temp, ... */
} __packed event_record_t;

/** Linear CR2032 map used on both boards (FR-S5). */
static inline uint8_t smart_tag_battery_percent(int32_t millivolts)
{
	if (millivolts >= SMART_TAG_BATTERY_FULL_MV) {
		return 100U;
	}

	if (millivolts <= SMART_TAG_BATTERY_EMPTY_MV) {
		return 0U;
	}

	return (uint8_t)(((millivolts - SMART_TAG_BATTERY_EMPTY_MV) * 100) /
			 (SMART_TAG_BATTERY_FULL_MV - SMART_TAG_BATTERY_EMPTY_MV));
}

/** Builds the flash record that sensor_log_add() appends. */
static inline void smart_tag_fill_log_record(sensor_log_record_t *out,
					     const sensor_data_t *data,
					     bool alarm)
{
	out->timestamp = data->timestamp;
	out->temperature = data->temperature_c_x100;
	out->humidity = data->humidity_x100;
	out->pressure = data->pressure_pa;
	out->gas_resistance_ohm = data->gas_resistance_ohm;
	out->flags = data->valid;

	if (data->motion) {
		out->flags |= SENSOR_FLAG_MOVING;
	}

	if (alarm) {
		out->flags |= SENSOR_FLAG_ALARM;
	}
}

#endif /* SMART_TAG_H */
