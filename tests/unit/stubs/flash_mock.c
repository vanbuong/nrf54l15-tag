/*
 * RAM-backed flash_area for host tests of flash_manager.c.
 *
 * Four 64-byte "sectors" (two 32-byte slots each) so wrap and sector
 * erase can be exercised without a 2 MB array.
 */

#include <string.h>
#include <errno.h>

#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>

#define MOCK_SECTOR_SIZE	64U
#define MOCK_SECTOR_COUNT	4U
#define MOCK_FLASH_SIZE		(MOCK_SECTOR_SIZE * MOCK_SECTOR_COUNT)
#define MOCK_ERASED		0xffU

static uint8_t mem[MOCK_FLASH_SIZE];
static struct flash_area area;
static struct device mock_dev = { .name = "mock-flash" };
static int opened;

void flash_mock_reset(void)
{
	memset(mem, MOCK_ERASED, sizeof(mem));
	opened = 0;
}

uint8_t *flash_mock_raw(void)
{
	return mem;
}

size_t flash_mock_size(void)
{
	return sizeof(mem);
}

int flash_area_open(uint8_t id, const struct flash_area **out)
{
	area.fa_id = id;
	area.fa_off = 0;
	area.fa_size = MOCK_FLASH_SIZE;
	*out = &area;
	opened = 1;
	return 0;
}

int flash_area_read(const struct flash_area *fa, off_t off, void *dst,
		    size_t len)
{
	if (fa == NULL || (size_t)off + len > fa->fa_size) {
		return -EINVAL;
	}

	memcpy(dst, mem + off, len);
	return 0;
}

int flash_area_write(const struct flash_area *fa, off_t off, const void *src,
		     size_t len)
{
	if (fa == NULL || (size_t)off + len > fa->fa_size) {
		return -EINVAL;
	}

	/* NOR-like: programming can only clear bits. */
	const uint8_t *bytes = src;

	for (size_t i = 0; i < len; i++) {
		mem[off + i] &= bytes[i];
	}

	return 0;
}

int flash_area_erase(const struct flash_area *fa, off_t off, size_t len)
{
	if (fa == NULL || (size_t)off + len > fa->fa_size) {
		return -EINVAL;
	}

	memset(mem + off, MOCK_ERASED, len);
	return 0;
}

const struct device *flash_area_get_device(const struct flash_area *fa)
{
	(void)fa;
	return &mock_dev;
}

int flash_get_page_info_by_offs(const struct device *dev, off_t offset,
				struct flash_pages_info *info)
{
	(void)dev;
	(void)offset;
	info->start_offset = 0;
	info->size = MOCK_SECTOR_SIZE;
	info->index = 0;
	return 0;
}
