# Hardware validation

PLAN.md phase 1. Run this before anything else on a new board. It proves every
peripheral works and settles the three questions the schematic review could not.

## Build and run

    west build -b holyiot_25025/nrf54l15/cpuapp -p always . \
        -- -DCONF_FILE=prj_validation.conf
    west flash

The product has no UART, so the report comes out over SEGGER RTT:

    JLinkRTTViewer

Press the button to run the suite again without reflashing.

This is a complete configuration in its own right, not a layer on top of
`prj.conf`. Bluetooth, Zigbee, the settings subsystem and every sensor driver
are absent, so a failure can only be hardware, pinctrl or the device tree.

Setting `CONF_FILE` explicitly also makes Zephyr skip `boards/<board>.conf`, so
the per-board sensor Kconfig the application build uses is not applied here.
That is intended - this build wants no sensor drivers on any board.

## Structure

The suite is in two halves, because the two supported boards share no sensors:

| File | Contents |
|---|---|
| `hw_validation.c` | the harness and the board-neutral tests - LEDs, button, battery, log storage |
| `hw_validation_holyiot.c` | SHT40, LIS2DH12, LPS22HB, BY25Q16 NOR |
| `hw_validation_tag.c` | BME688, ADXL367 (including its interrupt line), BMI270 |

`CMakeLists.txt` picks the board file with `dt_nodelabel()` rather than a
Kconfig test, since no sensor Kconfig is set in this build.

## Why raw register access

The suite does not use the Zephyr sensor drivers, on purpose.

A driver that refuses to bind reports one error for several very different
faults - swapped chip select, wrong part fitted, dead bus, bad pinctrl - and
during bring-up those need telling apart. A WHO_AM_I read on each chip select
says which. It also still works when the fitted part is not the one the driver
expects, which matters here: the Zephyr `lps22hh` driver rejects any WHO_AM_I
other than `0xB3`, and the schematic shows an LPS22HB, which answers `0xB1`.

Once this build passes, the application build exercises the same parts through
the drivers.

## What it checks

Both boards:

| Test | Verdict from |
|---|---|
| RGB LED | seven colours in sequence - **confirm by eye** |
| Log map | both log partitions open at the expected offsets and sizes |
| Log R/W | erase, blank check, write, read back, erase (**destructive**) |
| Battery | VDD reads between 1800 and 3600 mV |
| Button | a press and release within 10 s |

HOLyiot 25025 only:

| Test | Verdict from |
|---|---|
| I2C bus | at least one device answers a scan |
| SHT40 | soft reset, serial number with CRC, a measurement in a plausible range |
| SPI sensors | WHO_AM_I on both chip selects, each mapped to a known part |
| LIS2DH12 | configure, read all three axes, magnitude near 1 g at rest |
| LPS22 | configure, read pressure, between 800 and 1100 hPa |
| SPI NOR ID | JEDEC ID, compared against the `jedec-id` in the device tree |

nRF54L15 Tag only:

| Test | Verdict from |
|---|---|
| I2C bus | a scan finds both 0x76 (BME688) and 0x1D (ADXL367) |
| BME688 | chip ID register `0xD0` reads `0x61` |
| ADXL367 | `DEVID_AD` `0xAD`, `DEVID_MST` `0x1D`, `PARTID` `0xF2` |
| ADXL367 INT1 | the pin can be configured to interrupt, and an edge arrives |
| BMI270 | `CHIP_ID` reads `0x24` over SPI |

`ADXL367 INT1` is the test worth having on that board. It covers exactly what
the HOLyiot design could not do: there the accelerometer interrupts landed on
port P2, which has no GPIOTE and no SENSE, so `gpio_pin_interrupt_configure()`
returned `-ENOTSUP` and motion had to be polled. Here INT1 is on P0.03, and the
test checks both that the pin accepts an interrupt configuration and that the
part actually drives it.

It uses DATA_READY rather than activity detection on purpose. What can be wrong
in hardware is the electrical path and the SoC's ability to take an edge on that
pin; DATA_READY exercises that with three register writes, no thresholds to get
wrong, and nobody needing to shake the board. Activity detection is the driver's
business, and the application build exercises it.

The report ends with a count and lights the LED steady green if nothing failed,
steady red otherwise.

`RGB LED` always reports `CHECK`, not `PASS`: nothing reads the LED back, so
the firmware cannot know whether the colours actually appeared.

### The destructive log test

`Log R/W` erases the first sector of the `event-log` partition, writes a
pattern, verifies it and erases again. It is on by default because identifying
a storage device is not the same as proving it can be written, and the event
log is the right thing to sacrifice during bring-up. This is external SPI NOR
on the HOLyiot board and internal RRAM on the Tag; the test goes through the
`flash_area` API either way and takes the blank value from the driver rather
than assuming `0xff`. Set
`CONFIG_SMART_TAG_HW_VALIDATION_DESTRUCTIVE=n` to skip it.

## The two remaining open questions

Chip-select assignment was the third one - resolved: `CS_LIS2DH12` = P2.05,
`CS_LPS22HB` = P2.06, matching `hardware/pinmap.md` and the device tree.

A separate issue affected all three buses at one point or another and was
blamed on the nRF54L15 datasheet's pin assignment table each time - SPIM00,
SPIM21 and TWIM22 were all bit-banged over GPIO at some stage. None of it was
a pin restriction. The real causes were a wrong NOR flash `jedec-id`
(SPIM21), a wrong SHT40 address (TWIM22), and a `spi-max-frequency` of
10 MHz (SPIM00), which SPIM00's 128 MHz base clock cannot divide down to; at
8 MHz it initialises fine. All three now run as real hardware peripherals -
see README.md limitation #2. "SPI sensors" below checks `spi_is_ready_dt()`
on the real SPIM00 controller; "I2C bus" checks the real TWIM22 device.

### 1. LPS22HB or LPS22HH

| WHO_AM_I | Part | Consequence |
|---|---|---|
| `0xB1` | LPS22HB | the `lps22hh` driver will refuse to bind - see README |
| `0xB3` | LPS22HH | the application works as configured |
| `0xB4` | LPS22DF | needs the `lps2xdf` driver instead |

The validation build reads the sensor whichever it is, because it does not go
through the driver. The pressure reading it prints tells you the part works;
the ID tells you what the application build will do with it.

### 2. NOR flash JEDEC ID - resolved

`68 40 15` (assumed Boya BY25Q16BS) was wrong. The suite's raw RDID read
during bring-up consistently reported `68 10 15` instead - manufacturer (68)
and capacity (15, 16 Mbit) match, only the memory-type byte differs, most
likely a different BY25Q16B sub-variant than assumed. `jedec-id` in the
device tree and the expected value in `test_flash_id()` have both been
corrected to `68 10 15`.

## Not covered

- **Crystal accuracy.** The LFXO and HFXO load capacitors in the board DTS are
  nRF54L15 DK defaults and unverified. Measuring frequency error needs the
  radio running, so it belongs to phase 4, not here.
- **Current consumption.** Phase 6.
- **Accelerometer interrupt lines.** P2.00 and P2.03 cannot raise interrupts on
  this SoC - see the hardware limitations in README.md. Nothing to test.
- **Radio.** By design: this build has no protocol stack in it.

## After it passes

Move to the application build:

    west build -b holyiot_25025/nrf54l15/cpuapp -p always .

That exercises the same parts through the Zephyr drivers, which is the second
half of phase 1.
