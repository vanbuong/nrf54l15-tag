#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "motion_manager.h"
#include "motion_classifier.h"
#include "shock_detector.h"

LOG_MODULE_REGISTER(motion_manager, LOG_LEVEL_INF);

/*
 * Tamper heuristic. The PCB has no tamper switch, so removal has to be
 * inferred: a tag that has been undisturbed for a while, then takes a shock
 * and ends up facing a different way, has almost certainly been picked up or
 * prised off. Requiring all three conditions keeps handling noise and a
 * passing forklift from raising it.
 */
#define TAMPER_SETTLED_S 300

static tag_config_t config;

static motion_state_t state;
static orientation_t orientation;
static bool tamper;

static uint32_t last_active_time;	/* last sample that looked like motion */
static uint32_t motion_started_time;
static uint32_t stationary_since;
static uint32_t total_movement_s;

void motion_manager_init(const tag_config_t *cfg)
{
	config = *cfg;
	state = MOTION_STATE_STATIONARY;
	orientation = ORIENTATION_UNKNOWN;
	tamper = false;
	last_active_time = 0;
	motion_started_time = 0;
	stationary_since = 0;
	total_movement_s = 0;

	shock_detector_init();
}

void motion_manager_update_config(const tag_config_t *cfg)
{
	config = *cfg;
}

void motion_manager_process(sensor_data_t *data, struct motion_result *result)
{
	struct shock_result shock;
	orientation_t now_orientation;
	uint32_t now = data->timestamp;
	bool was_settled;

	memset(result, 0, sizeof(*result));

	if ((data->valid & SENSOR_VALID_ACCEL) == 0U) {
		result->motion_state = (uint8_t)state;
		result->orientation = (uint8_t)orientation;
		return;
	}

	/* How long the tag had been undisturbed before this sample. */
	was_settled = (state == MOTION_STATE_STATIONARY) &&
		      (now - stationary_since) >= TAMPER_SETTLED_S;

	shock_detector_process(data, config.shock_threshold_mg,
			       config.free_fall_threshold_mg, &shock);

	if (shock.shock) {
		result->events |= MOTION_EVENT_SHOCK;
		result->shock_mg = shock.peak_mg;
		data->shock = true;
	}

	if (shock.free_fall) {
		result->events |= MOTION_EVENT_FREE_FALL;
		data->free_fall = true;
	}

	/* Motion state, with a quiet-time hysteresis. */
	if (motion_classifier_is_active(data, config.motion_threshold_mg)) {
		last_active_time = now;

		if (state == MOTION_STATE_STATIONARY) {
			state = MOTION_STATE_MOVING;
			motion_started_time = now;
			result->events |= MOTION_EVENT_START;
		}
	} else if (state == MOTION_STATE_MOVING &&
		   (now - last_active_time) >= config.motion_timeout_s) {
		uint32_t duration = last_active_time - motion_started_time;

		state = MOTION_STATE_STATIONARY;
		stationary_since = now;
		total_movement_s += duration;

		result->events |= MOTION_EVENT_STOP;
		result->movement_duration_s = duration;
	}

	data->motion = (state == MOTION_STATE_MOVING);

	/* Orientation, only when an axis is clearly aligned with gravity. */
	now_orientation = motion_classifier_orientation(data);

	if (now_orientation != ORIENTATION_UNKNOWN &&
	    now_orientation != orientation) {
		bool first = (orientation == ORIENTATION_UNKNOWN);

		orientation = now_orientation;

		if (!first) {
			result->events |= MOTION_EVENT_ORIENTATION;

			/*
			 * Settled, then struck, then facing a different way:
			 * treat it as interference.
			 */
			if (was_settled && shock.shock && !tamper) {
				tamper = true;
				result->events |= MOTION_EVENT_TAMPER;
				LOG_WRN("tamper inferred: shock and reorientation after %u s at rest",
					now - stationary_since);
			}
		}
	}

	data->orientation = (uint8_t)orientation;
	result->orientation = (uint8_t)orientation;
	result->motion_state = (uint8_t)state;
}

motion_state_t motion_manager_state(void)
{
	return state;
}

orientation_t motion_manager_orientation(void)
{
	return orientation;
}

uint32_t motion_manager_total_movement_s(void)
{
	return total_movement_s;
}

bool motion_manager_tamper(void)
{
	return tamper;
}

void motion_manager_clear_tamper(void)
{
	tamper = false;
}
