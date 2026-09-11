#include <stdlib.h>
#include <string.h>

#include "shock_detector.h"
#include "motion_classifier.h"

/*
 * Once an impact is reported, the tag has to look calm again before another
 * one counts. Without this a 3 g drop spread over four samples would be
 * logged four times.
 */
#define REARM_DEVIATION_MG 300

static bool shock_latched;
static bool free_fall_latched;
static uint16_t peak_mg;

void shock_detector_init(void)
{
	shock_latched = false;
	free_fall_latched = false;
	peak_mg = 0;
}

void shock_detector_process(const sensor_data_t *data,
			    uint16_t shock_threshold_mg,
			    uint16_t free_fall_threshold_mg,
			    struct shock_result *result)
{
	int32_t deviation = motion_classifier_deviation(data);
	uint32_t magnitude = data->magnitude_mg;
	uint32_t excursion = (uint32_t)abs(deviation);

	memset(result, 0, sizeof(*result));

	/* Free fall: the tag is close to weightless. */
	if (magnitude < free_fall_threshold_mg) {
		if (!free_fall_latched) {
			free_fall_latched = true;
			result->free_fall = true;
		}
	} else if (magnitude > (uint32_t)free_fall_threshold_mg +
				       REARM_DEVIATION_MG) {
		free_fall_latched = false;
	}

	/* Shock: a large excursion either side of 1 g. */
	if (excursion > shock_threshold_mg) {
		uint16_t this_peak = (uint16_t)MIN(excursion, UINT16_MAX);

		if (this_peak > peak_mg) {
			peak_mg = this_peak;
		}

		if (!shock_latched) {
			shock_latched = true;
			result->shock = true;
			result->peak_mg = this_peak;
		} else if (this_peak > result->peak_mg) {
			/* Still in the same impact, but it got harder. */
			result->peak_mg = this_peak;
		}
	} else if (excursion < REARM_DEVIATION_MG) {
		shock_latched = false;
	}
}

uint16_t shock_detector_peak(void)
{
	return peak_mg;
}

void shock_detector_reset_peak(void)
{
	peak_mg = 0;
}
