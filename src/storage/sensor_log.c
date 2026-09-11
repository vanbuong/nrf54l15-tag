#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "sensor_log.h"

LOG_MODULE_REGISTER(sensor_log, LOG_LEVEL_INF);

static struct flash_log log_instance = {
	/* See the note in event_log.c on PARTITION_ID vs FIXED_PARTITION_ID. */
	.partition_id = PARTITION_ID(sensor_log_partition),
	.payload_size = sizeof(sensor_log_record_t),
	.name = "sensor history",
};

int sensor_log_init(void)
{
	return flash_log_init(&log_instance);
}

int sensor_log_add(const sensor_data_t *data, bool alarm, uint32_t *seq)
{
	/*
	 * flags is data->valid verbatim, so the SENSOR_VALID_* bit layout is
	 * literally the on-flash flag layout. A reader can tell a zero
	 * gas_resistance_ohm meaning "no gas sensor on this board" from one
	 * meaning "the BME688 read zero" by testing SENSOR_VALID_GAS.
	 */
	sensor_log_record_t record = {
		.timestamp = data->timestamp,
		.temperature = data->temperature_c_x100,
		.humidity = data->humidity_x100,
		.pressure = data->pressure_pa,
		.gas_resistance_ohm = data->gas_resistance_ohm,
		.flags = data->valid,
	};

	if (data->motion) {
		record.flags |= SENSOR_FLAG_MOVING;
	}

	if (alarm) {
		record.flags |= SENSOR_FLAG_ALARM;
	}

	return flash_log_append(&log_instance, &record, seq);
}

int sensor_log_read(uint32_t start_seq, sensor_log_record_t *out, size_t max)
{
	return flash_log_read(&log_instance, start_seq, out, max);
}

void sensor_log_get_info(struct flash_log_info *info)
{
	flash_log_get_info(&log_instance, info);
}

int sensor_log_clear(void)
{
	return flash_log_erase(&log_instance);
}
