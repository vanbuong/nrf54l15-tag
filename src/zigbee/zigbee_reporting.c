#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <string.h>

#include "zigbee_reporting.h"

LOG_MODULE_REGISTER(zigbee_reporting, LOG_LEVEL_INF);

/* Battery moves slowly; one percentage point is worth a report. */
#define BATTERY_DELTA_PERCENT 1

struct baseline {
	bool valid;
	uint32_t last_report_time;
	int16_t temperature;
	uint16_t humidity;
	int32_t pressure;
	uint8_t battery_percent;

	/* Tag Monitor state that must go out the moment it changes. */
	uint8_t motion_state;
	uint8_t orientation;
	uint8_t tamper;
	uint16_t alarm_flags;
	uint32_t shock_count;
	uint32_t free_fall_count;

	/*
	 * Gas resistance also lives on the Tag Monitor cluster, but unlike the
	 * rest of it this is a continuous quantity, so it gets a delta rather
	 * than an on-change test.
	 */
	uint32_t gas_resistance;
};

static struct baseline baseline;

void zigbee_reporting_init(void)
{
	memset(&baseline, 0, sizeof(baseline));
}

void zigbee_reporting_force(void)
{
	baseline.valid = false;
}

uint32_t zigbee_reporting_evaluate(const sensor_data_t *data,
				   const tag_status_t *status,
				   const tag_config_t *config)
{
	uint32_t report = 0;
	bool interval_elapsed;

	if (!baseline.valid) {
		/* First sample after boot or after joining: send everything. */
		return REPORT_TEMPERATURE | REPORT_HUMIDITY | REPORT_PRESSURE |
		       REPORT_BATTERY | REPORT_TAG_MONITOR;
	}

	interval_elapsed = (data->timestamp - baseline.last_report_time) >=
			   config->report_max_interval_s;

	if ((data->valid & SENSOR_VALID_TEMP_RH) != 0U) {
		if (abs(data->temperature_c_x100 - baseline.temperature) >=
		    (int)config->report_temp_delta_c_x100) {
			report |= REPORT_TEMPERATURE;
		}

		if (abs((int)data->humidity_x100 - (int)baseline.humidity) >=
		    (int)config->report_humidity_delta_x100) {
			report |= REPORT_HUMIDITY;
		}
	}

	if ((data->valid & SENSOR_VALID_PRESSURE) != 0U) {
		if (labs((long)data->pressure_pa - (long)baseline.pressure) >=
		    (long)config->report_pressure_delta_pa) {
			report |= REPORT_PRESSURE;
		}
	}

	if ((data->valid & SENSOR_VALID_BATTERY) != 0U) {
		if (abs((int)data->battery_percent -
			(int)baseline.battery_percent) >=
		    BATTERY_DELTA_PERCENT) {
			report |= REPORT_BATTERY;
		}
	}

	/*
	 * Gas resistance rides the Tag Monitor cluster (see
	 * TAG_ATTR_GAS_RESISTANCE), so a significant change has to raise
	 * REPORT_TAG_MONITOR rather than a report flag of its own. The delta
	 * is in ohms and defaults to 5 kOhm: BME688 gas resistance is tens to
	 * hundreds of kOhm and drifts continuously during burn-in, so a tight
	 * threshold would report on nothing but noise.
	 */
	if ((data->valid & SENSOR_VALID_GAS) != 0U &&
	    config->report_gas_delta_ohm != 0U) {
		uint32_t a = data->gas_resistance_ohm;
		uint32_t b = baseline.gas_resistance;

		if ((a > b ? a - b : b - a) >= config->report_gas_delta_ohm) {
			report |= REPORT_TAG_MONITOR;
		}
	}

	/*
	 * Motion, shock, free fall and tamper are reported immediately -
	 * PLAN.md section 6 lists all four as unconditional.
	 */
	if (status->motion_state != baseline.motion_state ||
	    status->orientation != baseline.orientation ||
	    status->tamper != baseline.tamper ||
	    status->alarm_flags != baseline.alarm_flags ||
	    status->shock_count != baseline.shock_count ||
	    status->free_fall_count != baseline.free_fall_count) {
		report |= REPORT_TAG_MONITOR;
	}

	if (interval_elapsed) {
		/* Heartbeat: prove the tag is alive even if nothing changed. */
		report |= REPORT_TEMPERATURE | REPORT_HUMIDITY |
			  REPORT_PRESSURE | REPORT_BATTERY | REPORT_TAG_MONITOR;
	}

	return report;
}

void zigbee_reporting_commit(const sensor_data_t *data,
			     const tag_status_t *status, uint32_t reported)
{
	if (reported == 0U) {
		return;
	}

	if (reported & REPORT_TEMPERATURE) {
		baseline.temperature = data->temperature_c_x100;
	}

	if (reported & REPORT_HUMIDITY) {
		baseline.humidity = data->humidity_x100;
	}

	if (reported & REPORT_PRESSURE) {
		baseline.pressure = data->pressure_pa;
	}

	if (reported & REPORT_BATTERY) {
		baseline.battery_percent = data->battery_percent;
	}

	if (reported & REPORT_TAG_MONITOR) {
		baseline.motion_state = status->motion_state;
		baseline.orientation = status->orientation;
		baseline.tamper = status->tamper;
		baseline.alarm_flags = status->alarm_flags;
		baseline.shock_count = status->shock_count;
		baseline.free_fall_count = status->free_fall_count;

		/*
		 * Only when the reading was real. Latching a zero from a board
		 * with no gas sensor would make the next genuine reading look
		 * like a full-scale jump.
		 */
		if ((data->valid & SENSOR_VALID_GAS) != 0U) {
			baseline.gas_resistance = data->gas_resistance_ohm;
		}
	}

	baseline.last_report_time = data->timestamp;
	baseline.valid = true;
}
