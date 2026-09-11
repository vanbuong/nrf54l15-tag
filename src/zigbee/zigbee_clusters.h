/*
 * ZCL cluster plumbing - PLAN.md section 5.
 *
 * Owns the attribute storage that ZBOSS holds pointers into, and the writes
 * that push application state into it. Split from zigbee_manager.c so the
 * manager is about the stack lifecycle and this file is about the data model.
 *
 * Everything here must run on the ZBOSS thread: ZCL attribute writes are not
 * safe from arbitrary Zephyr threads. The manager schedules the calls.
 */
#ifndef ZIGBEE_CLUSTERS_H
#define ZIGBEE_CLUSTERS_H

#include "../app/smart_tag.h"

/** Seeds every cluster attribute with its power-on value. */
void zigbee_clusters_init(const tag_config_t *config);

/** Writes the measurement clusters selected by @p report (REPORT_* mask). */
void zigbee_clusters_update(const sensor_data_t *data,
			    const tag_status_t *status,
			    uint32_t log_record_count,
			    const tag_config_t *config, uint32_t report);

/** Registers the endpoint with the application framework. */
void zigbee_clusters_register(void);

#endif /* ZIGBEE_CLUSTERS_H */
