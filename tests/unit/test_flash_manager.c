#include "unity.h"
#include "storage/flash_manager.h"
#include "flash_mock.h"

#include <errno.h>

static struct flash_log flog;

static void open_log(void)
{
	flash_mock_reset();
	flog = (struct flash_log){
		.partition_id = 1,
		.payload_size = 8,
		.name = "test",
	};
	TEST_ASSERT_EQUAL_INT(0, flash_log_init(&flog));
}

void test_flash_append_and_read(void)
{
	uint8_t rec[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	uint8_t out[8] = { 0 };
	uint32_t seq = 0;
	struct flash_log_info info;

	open_log();
	TEST_ASSERT_EQUAL_INT(0, flash_log_append(&flog, rec, &seq));
	TEST_ASSERT_EQUAL_UINT32(1, seq);
	TEST_ASSERT_EQUAL_INT(1, flash_log_read(&flog, 1, out, 1));
	TEST_ASSERT_EQUAL_MEMORY(rec, out, 8);

	flash_log_get_info(&flog, &info);
	TEST_ASSERT_EQUAL_UINT32(1, info.record_count);
	TEST_ASSERT_EQUAL_UINT32(1, info.oldest_seq);
	TEST_ASSERT_EQUAL_UINT32(1, info.newest_seq);
	TEST_ASSERT_EQUAL_UINT8(1, info.ready);
}

void test_flash_get_oldest(void)
{
	uint8_t a[8] = { 10 };
	uint8_t b[8] = { 20 };
	uint8_t out[8] = { 0 };

	open_log();
	TEST_ASSERT_EQUAL_INT(0, flash_log_append(&flog, a, NULL));
	TEST_ASSERT_EQUAL_INT(0, flash_log_append(&flog, b, NULL));
	TEST_ASSERT_EQUAL_INT(0, flash_log_get_oldest(&flog, out));
	TEST_ASSERT_EQUAL_UINT8(10, out[0]);
}

void test_flash_torn_write_skipped_on_rescan(void)
{
	uint8_t rec[8] = { 9, 9, 9, 9, 9, 9, 9, 9 };
	uint8_t out[8];
	struct flash_log_info info;

	open_log();
	TEST_ASSERT_EQUAL_INT(0, flash_log_append(&flog, rec, NULL));

	/* Corrupt the CRC of slot 0. */
	flash_mock_raw()[2] ^= 0xff;
	flash_mock_raw()[3] ^= 0xff;

	TEST_ASSERT_EQUAL_INT(0, flash_log_init(&flog));
	flash_log_get_info(&flog, &info);
	TEST_ASSERT_EQUAL_UINT32(0, info.record_count);
	TEST_ASSERT_EQUAL_INT(0, flash_log_read(&flog, 1, out, 1));
}

void test_flash_erase_resets_sequence(void)
{
	uint8_t rec[8] = { 1 };
	uint32_t seq = 0;
	struct flash_log_info info;

	open_log();
	TEST_ASSERT_EQUAL_INT(0, flash_log_append(&flog, rec, NULL));
	TEST_ASSERT_EQUAL_INT(0, flash_log_erase(&flog));
	flash_log_get_info(&flog, &info);
	TEST_ASSERT_EQUAL_UINT32(0, info.record_count);
	TEST_ASSERT_EQUAL_INT(0, flash_log_append(&flog, rec, &seq));
	TEST_ASSERT_EQUAL_UINT32(1, seq);
}

void test_flash_wrap_drops_oldest_sector(void)
{
	uint8_t rec[8] = { 0 };
	struct flash_log_info info;
	uint32_t seq = 0;
	/* 4 sectors × 2 slots = 8. Filling past that wraps. */
	const uint32_t capacity_slots = 8;

	open_log();
	flash_log_get_info(&flog, &info);
	TEST_ASSERT_EQUAL_UINT32(capacity_slots, info.capacity);

	for (uint32_t i = 0; i < capacity_slots; i++) {
		rec[0] = (uint8_t)(i + 1);
		TEST_ASSERT_EQUAL_INT(0, flash_log_append(&flog, rec, &seq));
	}

	flash_log_get_info(&flog, &info);
	TEST_ASSERT_EQUAL_UINT32(capacity_slots, info.record_count);
	TEST_ASSERT_EQUAL_UINT32(1, info.oldest_seq);

	rec[0] = 99;
	TEST_ASSERT_EQUAL_INT(0, flash_log_append(&flog, rec, &seq));

	flash_log_get_info(&flog, &info);
	TEST_ASSERT_TRUE(info.oldest_seq > 1);
	TEST_ASSERT_EQUAL_UINT32(seq, info.newest_seq);
	TEST_ASSERT_TRUE(info.record_count < capacity_slots + 1);
}

void test_flash_rejects_oversized_payload(void)
{
	flash_mock_reset();
	flog = (struct flash_log){
		.partition_id = 1,
		.payload_size = 64,
		.name = "too-big",
	};
	TEST_ASSERT_EQUAL_INT(-EINVAL, flash_log_init(&flog));
}

void test_flash_unready_returns_enodev(void)
{
	struct flash_log dead = { .name = "dead", .payload_size = 8 };
	uint8_t rec[8] = { 0 };

	TEST_ASSERT_EQUAL_INT(-ENODEV, flash_log_append(&dead, rec, NULL));
	TEST_ASSERT_EQUAL_INT(-ENODEV, flash_log_read(&dead, 1, rec, 1));
	TEST_ASSERT_EQUAL_INT(-ENODEV, flash_log_erase(&dead));
}
