/*
 * SoC watchdog. Counts only while the CPU is running (pause-in-sleep), so a
 * coin-cell tag can sleep for minutes without feeding, but a stuck I2C
 * transaction or a wedged workqueue still resets it.
 */
#ifndef SMART_TAG_WATCHDOG_H
#define SMART_TAG_WATCHDOG_H

int smart_tag_watchdog_init(void);

void smart_tag_watchdog_feed(void);

#endif
