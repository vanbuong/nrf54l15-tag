/*
 * Zigbee OTA - PLAN.md sections 2 and 13 (phase 7).
 *
 * Thin wrapper over the add-on's zigbee_fota library, which owns the OTA
 * Upgrade client endpoint and the image transfer. This file exists to keep
 * the FOTA event handling, the LED indication and the reboot decision out of
 * the stack lifecycle code.
 */
#ifndef ZIGBEE_OTA_H
#define ZIGBEE_OTA_H

#include <stdbool.h>
#include <stdint.h>

/** Starts the OTA client and confirms the running image to MCUboot. */
int zigbee_ota_init(void);

/** Forwarded from zboss_signal_handler(). */
void zigbee_ota_signal_handler(uint32_t bufid);

/** Forwarded from the ZCL device callback. Returns true if it handled it. */
bool zigbee_ota_zcl_cb(uint32_t bufid);

bool zigbee_ota_in_progress(void);

#endif /* ZIGBEE_OTA_H */
