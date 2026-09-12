#ifndef ZEPHYR_KERNEL_H
#define ZEPHYR_KERNEL_H

/*
 * Minimal host stub of Zephyr kernel types used by the units under test.
 * Not a Zephyr port — just enough for Unity to compile flash_manager.c
 * and app_config.c on a laptop.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef BIT
#define BIT(n) (1u << (n))
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#define ARG_UNUSED(x) (void)(x)
#define BUILD_ASSERT(cond, msg) _Static_assert(cond, msg)

#define K_FOREVER 0

struct k_mutex {
	int unused;
};

static inline void k_mutex_init(struct k_mutex *m)
{
	ARG_UNUSED(m);
}

static inline int k_mutex_lock(struct k_mutex *m, int timeout)
{
	ARG_UNUSED(m);
	ARG_UNUSED(timeout);
	return 0;
}

static inline int k_mutex_unlock(struct k_mutex *m)
{
	ARG_UNUSED(m);
	return 0;
}

#endif /* ZEPHYR_KERNEL_H */
