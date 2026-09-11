/*
 * Zigbee reporting policy - PLAN.md section 6.
 *
 * "Don't report every measurement." Environmental attributes are written to
 * the ZCL layer only when they have moved by more than the configured delta,
 * or when the maximum interval has elapsed. Motion, shock, free fall and
 * tamper go out immediately.
 *
 * This is separate from zigbee_clusters.c because it is policy, not
 * plumbing: it decides *whether* to write, the cluster layer decides *how*.
 */
#ifndef ZIGBEE_REPORTING_H
#define ZIGBEE_REPORTING_H

#include "../app/smart_tag.h"

/** Which attribute groups a sample should push to the ZCL layer. */
#define REPORT_TEMPERATURE	BIT(0)
#define REPORT_HUMIDITY		BIT(1)
#define REPORT_PRESSURE		BIT(2)
#define REPORT_BATTERY		BIT(3)
#define REPORT_TAG_MONITOR	BIT(4)

void zigbee_reporting_init(void);

/**
 * Decides what in @p data is worth reporting given @p config and what was
 * last sent. Returns a REPORT_* bitmask; zero means stay quiet.
 */
uint32_t zigbee_reporting_evaluate(const sensor_data_t *data,
				   const tag_status_t *status,
				   const tag_config_t *config);

/** Records what was actually sent, so the next comparison has a baseline. */
void zigbee_reporting_commit(const sensor_data_t *data,
			     const tag_status_t *status, uint32_t reported);

/** Forces the next evaluation to report everything, e.g. after joining. */
void zigbee_reporting_force(void);

#endif /* ZIGBEE_REPORTING_H */
