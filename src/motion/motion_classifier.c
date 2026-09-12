#include <stdlib.h>

#include "motion_classifier.h"

/* 1 g in milli-g. */
#define ONE_G_MG SMART_TAG_ONE_G_MG

/* An axis must hold at least half a g before it counts as "up" or "down". */
#define ORIENTATION_MIN_MG (ONE_G_MG / 2)

int32_t motion_classifier_deviation(const sensor_data_t *data)
{
	return (int32_t)data->magnitude_mg - ONE_G_MG;
}

bool motion_classifier_is_active(const sensor_data_t *data,
				 uint16_t threshold_mg)
{
	return abs(motion_classifier_deviation(data)) > (int32_t)threshold_mg;
}

orientation_t motion_classifier_orientation(const sensor_data_t *data)
{
	int32_t ax = abs(data->accel_x_mg);
	int32_t ay = abs(data->accel_y_mg);
	int32_t az = abs(data->accel_z_mg);

	if (az >= ax && az >= ay && az > ORIENTATION_MIN_MG) {
		return data->accel_z_mg > 0 ? ORIENTATION_Z_UP :
					      ORIENTATION_Z_DOWN;
	}

	if (ay >= ax && ay >= az && ay > ORIENTATION_MIN_MG) {
		return data->accel_y_mg > 0 ? ORIENTATION_Y_UP :
					      ORIENTATION_Y_DOWN;
	}

	if (ax > ORIENTATION_MIN_MG) {
		return data->accel_x_mg > 0 ? ORIENTATION_X_UP :
					      ORIENTATION_X_DOWN;
	}

	return ORIENTATION_UNKNOWN;
}
