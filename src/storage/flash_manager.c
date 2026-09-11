#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/sys/crc.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "flash_manager.h"

LOG_MODULE_REGISTER(flash_manager, LOG_LEVEL_INF);

#define RECORD_MAGIC 0x5a47u

struct slot_header {
	uint16_t magic;
	uint16_t crc;
} __packed;

/*
 * A slot on flash is: magic, CRC of (seq + payload), the sequence number,
 * then the payload. The sequence number lives inside the CRC-protected area
 * so a torn write cannot produce a plausible ordering.
 */
struct slot {
	struct slot_header hdr;
	uint32_t seq;
	uint8_t payload[FLASH_LOG_MAX_PAYLOAD - sizeof(uint32_t)];
} __packed;

BUILD_ASSERT(sizeof(struct slot) == FLASH_LOG_SLOT_SIZE, "bad slot layout");

#define MAX_PAYLOAD (sizeof(((struct slot *)0)->payload))

static uint16_t slot_crc(const struct slot *s)
{
	return crc16_ccitt(0xffff, (const uint8_t *)&s->seq,
			   sizeof(s->seq) + MAX_PAYLOAD);
}

static bool slot_valid(const struct slot *s)
{
	return s->hdr.magic == RECORD_MAGIC && s->hdr.crc == slot_crc(s);
}

static int read_slot(struct flash_log *log, uint32_t index, struct slot *s)
{
	return flash_area_read(log->area, (off_t)index * FLASH_LOG_SLOT_SIZE, s,
			       FLASH_LOG_SLOT_SIZE);
}

/*
 * Records are written in strictly increasing sequence order, so the newest
 * and oldest sequence numbers found on flash identify the head and tail
 * wherever the ring happens to have wrapped.
 */
static void scan(struct flash_log *log)
{
	struct slot s;
	uint32_t newest_seq = 0;
	uint32_t oldest_seq = UINT32_MAX;
	uint32_t newest_slot = 0;

	log->head_slot = 0;
	log->tail_slot = 0;
	log->used_slots = 0;
	log->next_seq = 1;

	for (uint32_t i = 0; i < log->total_slots; i++) {
		if (read_slot(log, i, &s) != 0 || !slot_valid(&s)) {
			continue;
		}

		log->used_slots++;

		if (s.seq > newest_seq) {
			newest_seq = s.seq;
			newest_slot = i;
		}

		if (s.seq < oldest_seq) {
			oldest_seq = s.seq;
			log->tail_slot = i;
		}
	}

	if (log->used_slots == 0U) {
		return;
	}

	log->head_slot = (newest_slot + 1U) % log->total_slots;
	log->next_seq = newest_seq + 1U;
}

int flash_log_init(struct flash_log *log)
{
	const struct device *dev;
	struct flash_pages_info page;
	int err;

	log->ready = false;
	k_mutex_init(&log->lock);

	if (log->payload_size == 0U || log->payload_size > MAX_PAYLOAD) {
		LOG_ERR("%s: payload of %u B does not fit a %u B slot", log->name,
			log->payload_size, FLASH_LOG_SLOT_SIZE);
		return -EINVAL;
	}

	err = flash_area_open(log->partition_id, &log->area);
	if (err) {
		LOG_ERR("%s: cannot open partition: %d", log->name, err);
		return err;
	}

	dev = flash_area_get_device(log->area);
	if (dev == NULL || !device_is_ready(dev)) {
		LOG_ERR("%s: flash device not ready", log->name);
		return -ENODEV;
	}

	err = flash_get_page_info_by_offs(dev, log->area->fa_off, &page);
	if (err) {
		LOG_ERR("%s: cannot read page info: %d", log->name, err);
		return err;
	}

	log->sector_size = page.size;
	log->slots_per_sector = (uint32_t)(page.size / FLASH_LOG_SLOT_SIZE);
	log->total_slots =
		(uint32_t)(log->area->fa_size / FLASH_LOG_SLOT_SIZE);

	if (log->slots_per_sector == 0U ||
	    log->total_slots < log->slots_per_sector) {
		LOG_ERR("%s: partition too small", log->name);
		return -EINVAL;
	}

	/* Keep the ring a whole number of sectors so wrapping stays aligned. */
	log->total_slots -= log->total_slots % log->slots_per_sector;

	scan(log);
	log->ready = true;

	LOG_INF("%s: %u/%u records, next seq %u", log->name, log->used_slots,
		log->total_slots, log->next_seq);

	return 0;
}

/* Erases the sector the head is entering. Caller holds the lock. */
static int make_room(struct flash_log *log)
{
	struct slot s;
	uint32_t lost = 0;
	int err;

	if (log->head_slot % log->slots_per_sector != 0U) {
		/* Mid-sector: the target slot is already erased. */
		return 0;
	}

	for (uint32_t i = 0; i < log->slots_per_sector; i++) {
		if (read_slot(log, log->head_slot + i, &s) == 0 &&
		    slot_valid(&s)) {
			lost++;
		}
	}

	err = flash_area_erase(log->area,
			       (off_t)log->head_slot * FLASH_LOG_SLOT_SIZE,
			       log->sector_size);
	if (err) {
		LOG_ERR("%s: erase failed: %d", log->name, err);
		return err;
	}

	if (lost > 0U) {
		log->used_slots -= MIN(log->used_slots, lost);
		/* Whatever is still valid starts at the next sector. */
		log->tail_slot = (log->head_slot + log->slots_per_sector) %
				 log->total_slots;
	}

	return 0;
}

int flash_log_append(struct flash_log *log, const void *record, uint32_t *seq)
{
	struct slot s;
	int err;

	if (!log->ready) {
		return -ENODEV;
	}

	k_mutex_lock(&log->lock, K_FOREVER);

	err = make_room(log);
	if (err) {
		goto out;
	}

	memset(&s, 0xff, sizeof(s));
	s.hdr.magic = RECORD_MAGIC;
	s.seq = log->next_seq;
	memset(s.payload, 0, sizeof(s.payload));
	memcpy(s.payload, record, log->payload_size);
	s.hdr.crc = slot_crc(&s);

	err = flash_area_write(log->area,
			       (off_t)log->head_slot * FLASH_LOG_SLOT_SIZE, &s,
			       FLASH_LOG_SLOT_SIZE);
	if (err) {
		LOG_ERR("%s: write failed: %d", log->name, err);
		goto out;
	}

	if (log->used_slots == 0U) {
		log->tail_slot = log->head_slot;
	}

	if (seq != NULL) {
		*seq = log->next_seq;
	}

	log->next_seq++;
	log->used_slots++;
	log->head_slot = (log->head_slot + 1U) % log->total_slots;

out:
	k_mutex_unlock(&log->lock);
	return err;
}

int flash_log_read(struct flash_log *log, uint32_t start_seq, void *out,
		   size_t max)
{
	uint8_t *dst = out;
	struct slot s;
	uint32_t first = 0;
	size_t found = 0;

	if (!log->ready) {
		return -ENODEV;
	}

	k_mutex_lock(&log->lock, K_FOREVER);

	/*
	 * Records sit in the ring in sequence order with no gaps, so the slot
	 * holding a given sequence number can be computed rather than searched
	 * for. Streaming the log then costs the records returned, not the size
	 * of the partition.
	 */
	if (log->used_slots > 0U && read_slot(log, log->tail_slot, &s) == 0 &&
	    slot_valid(&s) && start_seq > s.seq) {
		first = start_seq - s.seq;
	}

	for (uint32_t i = first; i < log->used_slots && found < max; i++) {
		uint32_t index = (log->tail_slot + i) % log->total_slots;

		if (read_slot(log, index, &s) != 0 || !slot_valid(&s)) {
			continue;
		}

		if (s.seq < start_seq) {
			continue;
		}

		memcpy(dst + found * log->payload_size, s.payload,
		       log->payload_size);
		found++;
	}

	k_mutex_unlock(&log->lock);

	return (int)found;
}

int flash_log_get_oldest(struct flash_log *log, void *out)
{
	struct slot s;
	int err = -ENOENT;

	if (!log->ready) {
		return -ENODEV;
	}

	k_mutex_lock(&log->lock, K_FOREVER);

	if (log->used_slots > 0U &&
	    read_slot(log, log->tail_slot, &s) == 0 && slot_valid(&s)) {
		memcpy(out, s.payload, log->payload_size);
		err = 0;
	}

	k_mutex_unlock(&log->lock);

	return err;
}

void flash_log_get_info(struct flash_log *log, struct flash_log_info *info)
{
	struct slot s;

	memset(info, 0, sizeof(*info));
	info->record_size = log->payload_size;

	if (!log->ready) {
		return;
	}

	k_mutex_lock(&log->lock, K_FOREVER);

	info->ready = 1U;
	info->capacity = log->total_slots;
	info->record_count = log->used_slots;

	if (log->used_slots > 0U) {
		if (read_slot(log, log->tail_slot, &s) == 0 && slot_valid(&s)) {
			info->oldest_seq = s.seq;
		}

		info->newest_seq = log->next_seq - 1U;
	}

	k_mutex_unlock(&log->lock);
}

int flash_log_erase(struct flash_log *log)
{
	int err;

	if (!log->ready) {
		return -ENODEV;
	}

	k_mutex_lock(&log->lock, K_FOREVER);

	err = flash_area_erase(log->area, 0,
			       (size_t)log->total_slots * FLASH_LOG_SLOT_SIZE);
	if (err == 0) {
		log->head_slot = 0;
		log->tail_slot = 0;
		log->used_slots = 0;
		log->next_seq = 1;
		LOG_INF("%s: erased", log->name);
	} else {
		LOG_ERR("%s: erase failed: %d", log->name, err);
	}

	k_mutex_unlock(&log->lock);

	return err;
}
