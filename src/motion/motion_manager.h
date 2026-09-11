/*
 * Motion manager - PLAN.md section 10.
 *
 * Owns the STATIONARY <-> MOVING state machine and turns classifier and
 * shock-detector output into events.
 *
 * PLAN.md section 10 asks for this to be interrupt driven. The seam that made
 * that possible without touching this file is that motion_manager_process()
 * does not care where its sample came from - it is handed a sensor_data_t and
 * has no opinion about what woke the thread that produced it.
 *
 * That seam has now paid off across both boards:
 *
 *   HOLyiot 25025  the LIS2DH12's INT1/INT2 land on port P2, which has no
 *                  GPIOTE and no SENSE/DETECT, so they can neither raise an
 *                  interrupt nor wake the SoC. Samples are purely polled.
 *
 *   nRF54L15 Tag   the ADXL367's INT1 is on P0.03 and does raise an
 *                  interrupt. power_manager arms it as an activity trigger,
 *                  so the sample this manager sees after movement arrives
 *                  milliseconds after the movement rather than at the next
 *                  tick - with no change here.
 *
 * Note that the interrupt reports "something moved", not which of free fall,
 * shock or orientation change it was. The classification below still runs on
 * the sample, exactly as it did when everything was polled.
 */
#ifndef MOTION_MANAGER_H
#define MOTION_MANAGER_H

#include "../app/smart_tag.h"

/** Bits returned by motion_manager_process(). */
#define MOTION_EVENT_START		BIT(0)
#define MOTION_EVENT_STOP		BIT(1)
#define MOTION_EVENT_SHOCK		BIT(2)
#define MOTION_EVENT_FREE_FALL		BIT(3)
#define MOTION_EVENT_ORIENTATION	BIT(4)
#define MOTION_EVENT_TAMPER		BIT(5)

struct motion_result {
	uint32_t events;	/* MOTION_EVENT_* bitmask */
	uint16_t shock_mg;
	uint8_t orientation;
	uint8_t motion_state;
	uint32_t movement_duration_s;	/* set with MOTION_EVENT_STOP */
};

void motion_manager_init(const tag_config_t *config);

/** Re-reads thresholds after a configuration change. */
void motion_manager_update_config(const tag_config_t *config);

/**
 * Runs one acceleration sample through the state machine. Fills @p data's
 * motion/shock/free_fall/orientation fields and reports what changed.
 */
void motion_manager_process(sensor_data_t *data, struct motion_result *result);

motion_state_t motion_manager_state(void);

orientation_t motion_manager_orientation(void);

uint32_t motion_manager_total_movement_s(void);

/** True while the tag believes it has been interfered with. */
bool motion_manager_tamper(void);

void motion_manager_clear_tamper(void);

#endif /* MOTION_MANAGER_H */
