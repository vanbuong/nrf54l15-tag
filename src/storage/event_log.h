/*
 * Event log - PLAN.md section 9.
 *
 * Records every state change worth explaining after the fact: motion, shock,
 * free fall, alarms, network transitions. Read back over BLE by the phone
 * tool and summarised into the Zigbee LogRecordCount attribute.
 */
#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include "flash_manager.h"
#include "app/smart_tag.h"

int event_log_init(void);

/** Timestamps and appends an event, returning its sequence number. */
int event_log_add(event_type_t type, event_severity_t severity, int16_t value,
		  uint32_t *seq);

int event_log_read(uint32_t start_seq, event_record_t *out, size_t max);

void event_log_get_info(struct flash_log_info *info);

int event_log_clear(void);

#endif /* EVENT_LOG_H */
