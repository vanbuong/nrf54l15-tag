/*
 * Motion classifier - PLAN.md section 10.
 *
 * Turns a raw acceleration vector into "is it moving" and "which way is up".
 * Pure functions, no state, so the decision rules can be reasoned about and
 * unit tested independently of the state machine that consumes them.
 */
#ifndef MOTION_CLASSIFIER_H
#define MOTION_CLASSIFIER_H

#include "app/smart_tag.h"

/**
 * Signed deviation of the vector magnitude from 1 g, in milli-g. Zero for a
 * tag at rest in any orientation; positive or negative while accelerating.
 */
int32_t motion_classifier_deviation(const sensor_data_t *data);

/** True when the deviation exceeds @p threshold_mg in either direction. */
bool motion_classifier_is_active(const sensor_data_t *data,
				 uint16_t threshold_mg);

/**
 * Dominant-axis orientation. Returns ORIENTATION_UNKNOWN when no axis is
 * clearly aligned with gravity, so a tumbling tag does not emit a stream of
 * spurious orientation changes.
 */
orientation_t motion_classifier_orientation(const sensor_data_t *data);

#endif /* MOTION_CLASSIFIER_H */
