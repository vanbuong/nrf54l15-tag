/*
 * Firmware-only board identity.
 *
 * Kept out of smart_tag.h so host unit tests do not need a device tree.
 * Include this before smart_tag.h if you need SMART_TAG_MODEL_NAME to be
 * the per-board string rather than the generic default.
 *
 * The BME688 alias is the node that only exists on the nRF54L15 Tag
 * overlay. A third board should add an explicit compatible or Kconfig
 * instead of extending this test — see doc/DESIGN.md §3.2.
 */
#ifndef SMART_TAG_BOARD_ID_H
#define SMART_TAG_BOARD_ID_H

#include <zephyr/devicetree.h>

#if DT_NODE_EXISTS(DT_ALIAS(bme688))
#define SMART_TAG_BOARD_NRF54L15TAG	1
#define SMART_TAG_MODEL_NAME		"nRF54L15 Tag"
#else
#define SMART_TAG_BOARD_HOLYIOT		1
#define SMART_TAG_MODEL_NAME		"HOLyiot 25025"
#endif

#endif /* SMART_TAG_BOARD_ID_H */
