/*
 * Power manager - PLAN.md section 11.
 *
 * Owns the sampling thread and the two duty cycles that decide the battery
 * life: how often the sensors are read, and how long BLE is allowed to
 * advertise.
 *
 * PLAN.md section 11 wants the tag to sleep between an RTC wake-up and an
 * accelerometer interrupt. Whether the second is available is a board
 * property, and the two supported boards differ:
 *
 *   nRF54L15 Tag   the ADXL367's INT1 is on P0.03, which has GPIOTE and
 *                  supports pin sense. Both wake-ups are available. The
 *                  sampling thread blocks on a semaphore that either the
 *                  interval or the activity interrupt releases, so motion is
 *                  reacted to in milliseconds and a stationary tag can sleep
 *                  at the plain configured interval.
 *
 *   HOLyiot 25025  the LIS2DH12's INT1/INT2 are on port P2, which has no
 *                  GPIOTE and no SENSE, so the accelerometer cannot raise an
 *                  interrupt at all. Only the RTC wake-up exists, the
 *                  sampling interval also sets the motion latency, and the
 *                  operating-mode divider is the only lever: a shipping tag
 *                  spends battery polling to catch impacts.
 *
 * sensor_manager_wake_source() is what distinguishes them at runtime; it
 * returns NULL on a board with no usable interrupt.
 */
#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "app/smart_tag.h"

int power_manager_init(void);

/** Starts the periodic sampling. Call once every subsystem is up. */
void power_manager_start(void);

/** Takes a sample now instead of waiting for the next tick. */
void power_manager_sample_now(void);

/**
 * Opens the BLE service window from PLAN.md section 11: BLE is off in normal
 * operation and comes up on request for ble_service_window_s, then stops.
 */
void power_manager_request_service_mode(void);

/** Closes the service window early. */
void power_manager_end_service_mode(void);

bool power_manager_service_mode_active(void);

/** Re-reads the sampling interval after a configuration change. */
void power_manager_config_changed(void);

#endif /* POWER_MANAGER_H */
