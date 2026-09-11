#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "event_log.h"

LOG_MODULE_REGISTER(event_log, LOG_LEVEL_INF);

static struct flash_log log_instance = {
	/*
	 * PARTITION_ID, not the FIXED_PARTITION_ID alias it replaced: that
	 * name is deprecated, and this one also resolves a
	 * zephyr,mapped-partition node, which is what the log partitions are
	 * on the nRF54L15 Tag (internal RRAM - no external flash is fitted).
	 */
	.partition_id = PARTITION_ID(event_log_partition),
	.payload_size = sizeof(event_record_t),
	.name = "event log",
};

int event_log_init(void)
{
	return flash_log_init(&log_instance);
}

int event_log_add(event_type_t type, event_severity_t severity, int16_t value,
		  uint32_t *seq)
{
	event_record_t record = {
		.timestamp = (uint32_t)(k_uptime_get() / 1000),
		.type = (uint8_t)type,
		.severity = (uint8_t)severity,
		.value = value,
	};

	LOG_DBG("event %u sev %u value %d", record.type, record.severity,
		record.value);

	return flash_log_append(&log_instance, &record, seq);
}

int event_log_read(uint32_t start_seq, event_record_t *out, size_t max)
{
	return flash_log_read(&log_instance, start_seq, out, max);
}

void event_log_get_info(struct flash_log_info *info)
{
	flash_log_get_info(&log_instance, info);
}

int event_log_clear(void)
{
	return flash_log_erase(&log_instance);
}
