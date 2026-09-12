# Smart Tag V1 pin map

Derived from schematic `25025-beacon-V1.0-sch.pdf`.

## I2C

P1.09 = HT_SCL
P1.11 = HT_SDA

SHT40-AD1B-R2 = 0x45 per the datasheet's "B" address variant, but the fitted
part answers on 0x44 (confirmed at bring-up with a logic analyzer) - a third
disagreement between this file's schematic-derived assumptions and real
hardware, see the note below.

## Sensor SPI (SPIM00 - fast domain, port P2 only)

P2.01 = SCK
P2.02 = SDI / MOSI
P2.04 = SDO / MISO

P2.05 = CS_LS2DH12
P2.06 = CS_LPS22HB

P2.03 = LIS2DH12 INT2
P2.00 = LIS2DH12 INT1

## SPI NOR (SPIM21 - separate bus, NOT shared with the sensors)

P1.04 = FLASH_CS
P1.05 = FLASH_SO
P1.06 = FLASH_SI
P1.07 = FLASH_SCLK

BY25Q16BSXIG = 16 Mbit = 2 MB

## RGB

P2.07 = LED_B
P1.10 = LED_G
P2.09 = LED_R

Common anode: GPIO active-low.

## Button

P1.13 = BUTTON

100 kΩ pull-up and switch to GND: active-low.

## Debug

P0.01 = SWDIO
P0.00 = SWCLK
P0.04 = /RESET

## Important schematic observations

The NOR flash has its own SCLK/SI/SO on P1.07/P1.06/P1.05 - it does not sit on
the P2.01/02/04 sensor SPI bus. Two SPIM instances are therefore required.

The uploaded schematic identifies the pressure sensor as **LPS22HB**, and
bring-up confirmed it (WHO_AM_I `0xB1`). Zephyr's SPI-capable `st,lps22hh`
driver rejects any part whose WHO_AM_I is not `0xB3`, so a local driver
(`drivers/sensor/lps22hb_spi/`, `holyiot,lps22hb` compatible) is bound
instead - see the top-level README's LPS22HB/LPS22HH note.

This file has repeatedly disagreed with the confirmed-correct board
`.dts`/hardware (the SHT40 SCL pin, the LIS2DH12/LPS22HB chip-select
assignment, and the SHT40 I2C address above - all originally transcribed
wrong from the schematic/datasheet). Treat it as a starting point to check
against the schematic and real hardware, not as the final authority.

All three buses run as hardware peripherals. An earlier reading of the
nRF54L15 pin-assignment table suggested P2.01 / P1.07 / P1.09 were not
clock-capable; that was wrong. The failures were a wrong NOR `jedec-id`,
a wrong SHT40 address, and a 10 MHz `spi-max-frequency` that SPIM00 cannot
divide. At 8 MHz every instance initialises. See the top-level README's
"Bus topology" section and `boards/holyiot/holyiot_25025/README.md`.

LIS2DH12 INT1/INT2 on P2.00/P2.03 still cannot raise a GPIO interrupt:
port P2 has no GPIOTE and no SENSE. Motion is polled on this board.
