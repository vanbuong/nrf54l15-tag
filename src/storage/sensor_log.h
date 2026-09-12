/*
 * Sensor history - PLAN.md section 9.
 *
 * A slow, dense record of environmental readings, kept separately from the
 * event log so that a burst of shock events cannot evict the climate history
 * a shipping audit needs (and vice versa).
 */
#ifndef SENSOR_LOG_H
#define SENSOR_LOG_H

#include "flash_manager.h"
#include "app/smart_tag.h"

int sensor_log_init(void);

/** Appends one reading, deriving the record flags from @p data. */
int sensor_log_add(const sensor_data_t *data, bool alarm, uint32_t *seq);

int sensor_log_read(uint32_t start_seq, sensor_log_record_t *out, size_t max);

void sensor_log_get_info(struct flash_log_info *info);

int sensor_log_clear(void);

#endif /* SENSOR_LOG_H */
