/*
 * Smart Tag shared types.
 *
 * Implements the data model from PLAN.md sections 4 (sensor abstraction),
 * 9 (flash records) and 14 (product identity / operating modes).
 *
 * Every struct that crosses a boundary - a BLE GATT payload or a flash record
 * - is packed and little-endian so the layout is stable across firmware
 * versions and readable by a phone or PC tool.
 */
#ifndef SMART_TAG_H
#define SMART_TAG_H

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * Bumped whenever a wire struct below changes shape.
 *
 * v3: the nRF54L15 Tag port. sensor_data_t gained gas resistance (BME688)
 *     and a gyroscope (BMI270), sensor_log_record_t gained gas, and
 *     sensor_data_t became __packed - it was the one wire struct that was
 *     not, which left a compiler padding hole a decoder had to know about.
 */
#define SMART_TAG_PROTOCOL_VERSION 3

/*
 * Identity is per-board. Both targets run the same protocol; only the
 * strings differ. Keyed off the BME688 alias because that is the one node
 * that only ever exists on the Tag.
 */
#define SMART_TAG_HW_VERSION	1

#if DT_NODE_EXISTS(DT_ALIAS(bme688))
#define SMART_TAG_MODEL_NAME	"nRF54L15 Tag"
#else
#define SMART_TAG_MODEL_NAME	"HOLyiot 25025"
#endif

/*
 * PLAN.md section 14: one product, one firmware, several roles. The mode
 * selects sampling emphasis and reporting policy, not a different build.
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

/* Event types, stored in event_record_t.type. */
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
	/*
	 * Protocol v3, appended so every value above keeps its number and an
	 * older decoder still reads existing logs correctly.
	 */
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
/*
 * Protocol v3. alarm_flags is 16 bits, so bits 8-15 were free - unlike the
 * SENSOR_VALID_* byte, which this port filled.
 */
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
/* Protocol v3, nRF54L15 Tag only: BME688 gas and BMI270 gyroscope. */
#define SENSOR_VALID_GAS	BIT(4)
#define SENSOR_VALID_GYRO	BIT(5)
/* Extra flag bits carried in the log record only. */
#define SENSOR_FLAG_MOVING	BIT(6)
#define SENSOR_FLAG_ALARM	BIT(7)

/**
 * The common sensor structure from PLAN.md section 4, extended with the
 * battery fields that section 2 lists as a V1 requirement and a timestamp so
 * a reading can be logged without a second lookup.
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

	/*
	 * Protocol v3. Both are nRF54L15 Tag only; on a board without the
	 * part they stay zero and the matching SENSOR_VALID_* bit stays clear.
	 */
	uint32_t gas_resistance_ohm;	/* BME688, ohms */
	int16_t gyro_x_dps_x10;		/* BMI270, 0.1 deg/s */
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
 * characteristics; thresholds from PLAN.md section 8, reporting deltas from
 * section 6, the BLE service window from section 11.
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

	/* PLAN.md section 6: report on change, or when max interval expires. */
	uint16_t report_temp_delta_c_x100;
	uint16_t report_humidity_delta_x100;
	uint16_t report_pressure_delta_pa;
	uint16_t report_max_interval_s;

	/* PLAN.md section 11: BLE is off until asked for, then times out. */
	uint16_t ble_service_window_s;

	/*
	 * Protocol v3, BME688 only. Gas resistance falls as VOC concentration
	 * rises, so the alarm fires on a LOW reading, the opposite sense to
	 * the temperature and humidity ceilings above. Zero disables it.
	 */
	uint32_t gas_low_threshold_ohm;
	uint32_t report_gas_delta_ohm;

	uint8_t operating_mode;		/* tag_mode_t */
	uint8_t led_enabled;
	uint8_t sensor_history_enabled;
	uint8_t reserved;
} __packed tag_config_t;

/*
 * shock_threshold_mg is 4000, not the 2000 it was before the Tag port. On the
 * HOLyiot board the LIS2DH12 was fixed at +/-2 g, so a 2000 mg threshold sat
 * exactly at full scale and any real impact clipped against it. The ADXL367
 * reaches +/-8 g and the BMI270 +/-16 g, so the default can sit where a
 * genuine shock is rather than at the sensor's ceiling.
 *
 * gas_low_threshold_ohm defaults to 0 (disabled): a useful BME688 gas alarm
 * needs a per-unit baseline captured after the sensor burns in, not a
 * constant, so the threshold is left for the operator to set over BLE.
 */
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
	.report_humidity_delta_x100 = 300,		\
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

/** Lifetime device statistics, per PLAN.md section 2 ("Device statistics"). */
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

/* ---------------------------------------------------------------------------
 * Flash record layouts, PLAN.md section 9
 * ------------------------------------------------------------------------ */

/*
 * 17 bytes, against the 28 that fit a flash_manager slot (FLASH_LOG_SLOT_SIZE
 * 32 minus the 4-byte header). The gyroscope is deliberately NOT logged: it
 * is meaningful only while the tag is moving, changes far faster than the
 * sampling interval, and would roughly double the record for data nothing
 * reads back. It stays live-only, on the Sensor Data characteristic.
 */
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

#endif /* SMART_TAG_H */
