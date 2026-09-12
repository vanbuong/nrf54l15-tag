/*
 * Threshold / alarm policy (FR-A2, FR-A3).
 *
 * Zephyr-free so Unity can lock latching, hysteresis and the gas floor
 * without bringing up app_state.c.
 */
#ifndef APP_ALARMS_H
#define APP_ALARMS_H

#include "smart_tag.h"

#define APP_ALARM_MAX_EVENTS 8

struct app_alarm_event {
	uint8_t type;		/* event_type_t */
	uint8_t severity;	/* event_severity_t */
	int16_t value;
};

/**
 * Updates @p alarm_flags from one sample. Newly raised alarms are appended
 * to @p events (at most APP_ALARM_MAX_EVENTS). @p expected_valid is the
 * SENSOR_VALID_* mask this board must produce; extra bits (gyro) are not
 * a fault.
 */
void app_alarms_eval(const sensor_data_t *data, const tag_config_t *config,
		     uint8_t expected_valid, uint16_t *alarm_flags,
		     struct app_alarm_event *events, uint8_t *n_events);

#endif /* APP_ALARMS_H */
