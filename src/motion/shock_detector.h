/*
 * Shock and free-fall detection - PLAN.md section 10.
 *
 * Kept separate from the motion state machine because these are impulse
 * events, not states: they fire once per impact and carry a peak magnitude
 * that the state machine has no business tracking.
 */
#ifndef SHOCK_DETECTOR_H
#define SHOCK_DETECTOR_H

#include "../app/smart_tag.h"

struct shock_result {
	bool shock;
	bool free_fall;
	uint16_t peak_mg;	/* deviation from 1 g at the shock */
};

void shock_detector_init(void);

/**
 * Examines one acceleration sample.
 *
 * Both detections latch until the tag returns to rest, so a single impact
 * spanning several samples counts once rather than once per sample.
 */
void shock_detector_process(const sensor_data_t *data,
			    uint16_t shock_threshold_mg,
			    uint16_t free_fall_threshold_mg,
			    struct shock_result *result);

/** Highest peak seen since the last reset, in milli-g. */
uint16_t shock_detector_peak(void);

void shock_detector_reset_peak(void);

#endif /* SHOCK_DETECTOR_H */
