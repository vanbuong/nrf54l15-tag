/*
 * RGB LED manager - PLAN.md section 2.
 *
 * Colour meanings are fixed by the plan:
 *
 *   Blue    BLE advertising
 *   Green   Zigbee connected
 *   Yellow  Zigbee joining
 *   Purple  OTA in progress
 *   Red     alarm
 *   White   identify
 *
 * Several of these can be true at once, so the manager keeps a set of active
 * indications and shows the highest priority one rather than letting whichever
 * subsystem called last win.
 */
#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

/* Listed low to high priority; identify always wins so a user can find a tag. */
typedef enum {
	LED_STATE_IDLE = 0,
	LED_STATE_BLE_ADVERTISING,
	LED_STATE_ZIGBEE_JOINING,
	LED_STATE_ZIGBEE_CONNECTED,
	LED_STATE_OTA,
	LED_STATE_ALARM,
	LED_STATE_IDENTIFY,
	LED_STATE_COUNT,
} led_state_t;

int led_manager_init(void);

/** Adds or removes one indication from the active set. */
void led_manager_set(led_state_t state, bool active);

/** Identify for a fixed period, then clear itself. */
void led_manager_identify(uint16_t duration_s);

/** One-off acknowledgement flash that does not disturb the active set. */
void led_manager_flash(led_state_t state, uint8_t count);

/** Master switch, driven by the persisted led_enabled configuration flag. */
void led_manager_set_enabled(bool enabled);

#endif /* LED_MANAGER_H */
