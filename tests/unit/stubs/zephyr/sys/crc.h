#ifndef ZEPHYR_SYS_CRC_H
#define ZEPHYR_SYS_CRC_H

#include <stddef.h>
#include <stdint.h>

uint16_t crc16_ccitt(uint16_t seed, const uint8_t *src, size_t len);

#endif
