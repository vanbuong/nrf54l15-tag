#include <stddef.h>
#include <stdint.h>

#include "app_alarms.h"

static void emit(struct app_alarm_event *events, uint8_t *n_events,
		 event_type_t type, event_severity_t severity, int16_t value)
{
	if (events == NULL || n_events == NULL ||
	    *n_events >= APP_ALARM_MAX_EVENTS) {
		return;
	}

	events[*n_events].type = (uint8_t)type;
	events[*n_events].severity = (uint8_t)severity;
	events[*n_events].value = value;
	(*n_events)++;
}

void app_alarms_eval(const sensor_data_t *data, const tag_config_t *config,
		     uint8_t expected_valid, uint16_t *alarm_flags,
		     struct app_alarm_event *events, uint8_t *n_events)
{
	uint16_t flags;

	if (data == NULL || config == NULL || alarm_flags == NULL) {
		return;
	}

	flags = *alarm_flags;
	if (n_events != NULL) {
		*n_events = 0;
	}

	if ((data->valid & SENSOR_VALID_TEMP_RH) != 0U) {
		if (data->temperature_c_x100 > config->temp_high_c_x100) {
			if ((flags & ALARM_TEMP_HIGH) == 0U) {
				flags |= ALARM_TEMP_HIGH;
				emit(events, n_events, EVENT_TEMP_HIGH,
				     EVENT_SEVERITY_ALARM,
				     data->temperature_c_x100);
			}
		} else if (data->temperature_c_x100 < config->temp_low_c_x100) {
			if ((flags & ALARM_TEMP_LOW) == 0U) {
				flags |= ALARM_TEMP_LOW;
				emit(events, n_events, EVENT_TEMP_LOW,
				     EVENT_SEVERITY_ALARM,
				     data->temperature_c_x100);
			}
		} else {
			flags &= (uint16_t)~(ALARM_TEMP_HIGH | ALARM_TEMP_LOW);
		}

		if (data->humidity_x100 > config->humidity_high_x100) {
			if ((flags & ALARM_HUMIDITY_HIGH) == 0U) {
				flags |= ALARM_HUMIDITY_HIGH;
				emit(events, n_events, EVENT_HUMIDITY_HIGH,
				     EVENT_SEVERITY_WARNING,
				     (int16_t)data->humidity_x100);
			}
		} else {
			flags &= (uint16_t)~ALARM_HUMIDITY_HIGH;
		}
	}

	/*
	 * Gas alarm is a floor: BME688 resistance falls as VOC rises.
	 * Threshold 0 disables the check.
	 */
	if ((data->valid & SENSOR_VALID_GAS) != 0U &&
	    config->gas_low_threshold_ohm != 0U) {
		if (data->gas_resistance_ohm < config->gas_low_threshold_ohm) {
			if ((flags & ALARM_GAS_LOW) == 0U) {
				flags |= ALARM_GAS_LOW;
				emit(events, n_events, EVENT_GAS_LOW,
				     EVENT_SEVERITY_WARNING,
				     (int16_t)MIN(data->gas_resistance_ohm / 1000U,
						  (uint32_t)INT16_MAX));
			}
		} else {
			flags &= (uint16_t)~ALARM_GAS_LOW;
		}
	}

	if ((data->valid & SENSOR_VALID_BATTERY) != 0U) {
		if (data->battery_mv < config->battery_low_mv) {
			if ((flags & ALARM_BATTERY_LOW) == 0U) {
				flags |= ALARM_BATTERY_LOW;
				emit(events, n_events, EVENT_BATTERY_LOW,
				     EVENT_SEVERITY_WARNING,
				     (int16_t)data->battery_mv);
			}
		} else if (data->battery_mv >=
			   (uint16_t)(config->battery_low_mv +
				      SMART_TAG_BATTERY_HYSTERESIS_MV)) {
			flags &= (uint16_t)~ALARM_BATTERY_LOW;
		}
	}

	if ((data->valid & expected_valid) != expected_valid) {
		if ((flags & ALARM_SENSOR_FAULT) == 0U) {
			flags |= ALARM_SENSOR_FAULT;
			emit(events, n_events, EVENT_SENSOR_FAULT,
			     EVENT_SEVERITY_WARNING, (int16_t)data->valid);
		}
	} else {
		flags &= (uint16_t)~ALARM_SENSOR_FAULT;
	}

	*alarm_flags = flags;
}
