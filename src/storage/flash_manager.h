/*
 * Flash manager - PLAN.md section 9.
 *
 * A fixed-record circular logger over a flash partition. Deliberately not a
 * filesystem: records are a constant size, appended in order, and the oldest
 * sector is erased when the ring wraps. That makes power-loss recovery a
 * matter of skipping records whose CRC does not match, with no metadata to
 * repair.
 *
 * One instance is created per log (event log, sensor history).
 */
#ifndef FLASH_MANAGER_H
#define FLASH_MANAGER_H

#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <stdint.h>

/* Every slot is this size; the payload plus a 4-byte magic/CRC header. */
#define FLASH_LOG_SLOT_SIZE	32U
#define FLASH_LOG_HEADER_SIZE	4U
#define FLASH_LOG_MAX_PAYLOAD	(FLASH_LOG_SLOT_SIZE - FLASH_LOG_HEADER_SIZE)

struct flash_log {
	/* Configuration, set by the owner before flash_log_init(). */
	uint8_t partition_id;
	uint16_t payload_size;
	const char *name;

	/* Runtime state, owned by flash_manager.c. */
	const struct flash_area *area;
	struct k_mutex lock;
	size_t sector_size;
	uint32_t total_slots;
	uint32_t slots_per_sector;
	uint32_t head_slot;
	uint32_t tail_slot;
	uint32_t used_slots;
	uint32_t next_seq;
	bool ready;
};

struct flash_log_info {
	uint32_t record_count;
	uint32_t capacity;
	uint32_t oldest_seq;
	uint32_t newest_seq;
	uint16_t record_size;
	uint8_t ready;
	uint8_t reserved;
} __packed;

/**
 * Opens the partition and rebuilds the ring bounds by scanning it. Safe to
 * call on a blank or partially written partition.
 */
int flash_log_init(struct flash_log *log);

/**
 * Appends one record. Returns the assigned sequence number through @p seq
 * when it is not NULL. Erases the oldest sector if the ring has wrapped.
 */
int flash_log_append(struct flash_log *log, const void *record, uint32_t *seq);

/**
 * Reads up to @p max records with sequence number >= @p start_seq, oldest
 * first, into a caller array of @p log->payload_size stride. Returns the
 * count read or a negative errno.
 */
int flash_log_read(struct flash_log *log, uint32_t start_seq, void *out,
		   size_t max);

/** The oldest record still held, for the get_oldest() call in PLAN.md. */
int flash_log_get_oldest(struct flash_log *log, void *out);

void flash_log_get_info(struct flash_log *log, struct flash_log_info *info);

/** Erases the partition and restarts sequence numbering at 1. */
int flash_log_erase(struct flash_log *log);

#endif /* FLASH_MANAGER_H */
