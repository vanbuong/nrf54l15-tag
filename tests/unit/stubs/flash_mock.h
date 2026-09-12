#ifndef FLASH_MOCK_H
#define FLASH_MOCK_H

#include <stddef.h>
#include <stdint.h>

void flash_mock_reset(void);
uint8_t *flash_mock_raw(void);
size_t flash_mock_size(void);

#endif
