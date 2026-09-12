#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/crc.h>

/* CRC-16-CCITT (poly 0x1021), matching Zephyr crc16_ccitt(). */
uint16_t crc16_ccitt(uint16_t seed, const uint8_t *src, size_t len)
{
	uint16_t crc = seed;

	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)src[i] << 8;
		for (int bit = 0; bit < 8; bit++) {
			if (crc & 0x8000U) {
				crc = (uint16_t)((crc << 1) ^ 0x1021U);
			} else {
				crc = (uint16_t)(crc << 1);
			}
		}
	}

	return crc;
}
