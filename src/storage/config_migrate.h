/*
 * Settings blob encode/decode with a schema header.
 *
 * Protocol v3 wrote tag_config_t / tag_statistics_t verbatim. From
 * schema 1 the NVS value is:
 *
 *   uint16_t schema
 *   uint16_t body_size
 *   body bytes (may be shorter than the running struct)
 *
 * A shorter body is copied onto defaults so new fields survive an OTA.
 * Unknown trailing bytes are ignored. This file has no Zephyr dependency
 * so Unity can lock the migrate cases.
 */
#ifndef CONFIG_MIGRATE_H
#define CONFIG_MIGRATE_H

#include <stddef.h>
#include <stdint.h>

#include "app/smart_tag.h"

#define TAG_CONFIG_SCHEMA	1
#define TAG_STATS_SCHEMA	1

/* Protocol v3 on-disk sizes, before the schema header existed. */
#define TAG_CONFIG_V0_SIZE	40U
#define TAG_STATS_V0_SIZE	40U

struct tag_settings_header {
	uint16_t schema;
	uint16_t body_size;
} __packed;

#define TAG_CONFIG_FRAMED_SIZE							\
	(sizeof(struct tag_settings_header) + sizeof(tag_config_t))
#define TAG_STATS_FRAMED_SIZE							\
	(sizeof(struct tag_settings_header) + sizeof(tag_statistics_t))

_Static_assert(TAG_CONFIG_FRAMED_SIZE != TAG_CONFIG_V0_SIZE,
	       "framed config must not collide with v0 length");
_Static_assert(TAG_STATS_FRAMED_SIZE != TAG_STATS_V0_SIZE,
	       "framed stats must not collide with v0 length");

int tag_config_decode(const void *blob, size_t len, tag_config_t *out);
size_t tag_config_encode(const tag_config_t *in, void *blob, size_t max);

int tag_stats_decode(const void *blob, size_t len, tag_statistics_t *out);
size_t tag_stats_encode(const tag_statistics_t *in, void *blob, size_t max);

#endif /* CONFIG_MIGRATE_H */
