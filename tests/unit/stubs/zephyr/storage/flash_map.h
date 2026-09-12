#ifndef ZEPHYR_STORAGE_FLASH_MAP_H
#define ZEPHYR_STORAGE_FLASH_MAP_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct device;

struct flash_area {
	uint8_t fa_id;
	off_t fa_off;
	size_t fa_size;
};

int flash_area_open(uint8_t id, const struct flash_area **area);
int flash_area_read(const struct flash_area *area, off_t off, void *dst,
		    size_t len);
int flash_area_write(const struct flash_area *area, off_t off, const void *src,
		     size_t len);
int flash_area_erase(const struct flash_area *area, off_t off, size_t len);
const struct device *flash_area_get_device(const struct flash_area *area);

#endif
