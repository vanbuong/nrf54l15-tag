#ifndef ZEPHYR_DRIVERS_FLASH_H
#define ZEPHYR_DRIVERS_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct device {
	const char *name;
};

struct flash_pages_info {
	off_t start_offset;
	size_t size;
	uint32_t index;
};

int flash_get_page_info_by_offs(const struct device *dev, off_t offset,
				struct flash_pages_info *info);

static inline bool device_is_ready(const struct device *dev)
{
	return dev != NULL;
}

#endif
