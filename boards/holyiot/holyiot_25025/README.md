# holyiot_25025 - HOLyiot 25025 Beacon V1.0

Out-of-tree board definition for the HOLyiot 25025 nRF54L15 Smart Tag,
derived from `hardware/25025-beacon-V1.0-sch.pdf`.

Build target:

    west build -b holyiot_25025/nrf54l15/cpuapp -p always .

The application directory is automatically a `BOARD_ROOT`, so no extra CMake
or west arguments are needed.

## SoC

nRF54L15-SOC1-QFAA-AB0, application core only (`cpuapp`). The FLPR core is not
used by this product.

## Peripherals

| Bus | Instance | Pins | Devices |
|---|---|---|---|
| Sensor SPI | SPIM00 (hardware) | SCK P2.01, MOSI P2.02, MISO P2.04 | LIS2DH12 (CS P2.05), LPS22 (CS P2.06) |
| Flash SPI | SPIM21 (hardware) | SCLK P1.07, MOSI P1.06, MISO P1.05 | BY25Q16BSXIG 16 Mbit (CS P1.04) |
| I2C | TWIM22 (hardware) | SCL P1.09, SDA P1.11 | SHT40-AD1B-R2 @ 0x44 |

All three run as real hardware peripherals. Note that SPIM00 and SPIM21
have different base clocks (128 MHz and 16 MHz), so `spi-max-frequency`
values are not interchangeable between them - see limitation #2 below.

| Function | Pin | Alias |
|---|---|---|
| RGB red | P2.09 | `led0` / `led-red` |
| RGB green | P1.10 | `led1` / `led-green` |
| RGB blue | P2.07 | `led2` / `led-blue` |
| User button | P1.13 | `sw0` / `user-button` |
| LIS2DH12 INT1 | P2.00 | - |
| LIS2DH12 INT2 | P2.03 | - |

The RGB LED is common anode, so all three LED GPIOs are active low. The button
has a 100 kOhm pull-up and switches to GND, so it is active low.

## Console

The product exposes no UART, only SWD. The board enables `CONFIG_CONSOLE` but
selects no backend and sets no `zephyr,console` chosen node; the application
picks SEGGER RTT:

    CONFIG_USE_SEGGER_RTT=y
    CONFIG_RTT_CONSOLE=y

## Known hardware limitations

### 1. The accelerometer interrupts cannot wake the SoC

LIS2DH12 INT1/INT2 land on P2.00/P2.03. Port P2 on nRF54L15 has **no GPIOTE
and no SENSE/DETECT mechanism**, so those lines cannot raise a GPIO interrupt
and cannot wake the device from sleep.

Consequences:

- Do not enable `CONFIG_LIS2DH_TRIGGER_GLOBAL_THREAD` or
  `CONFIG_LIS2DH_TRIGGER_OWN_THREAD`. `gpio_pin_interrupt_configure()` returns
  `-ENOTSUP` and the driver's init fails, leaving the sensor unusable.
- Motion detection must be polled on this board revision.
- A board respin that routes INT1 to P1 (or P0, which additionally supports
  the pin-sense wake mechanism) is required for interrupt-driven,
  low-power motion wake-up.

### 2. `spi-max-frequency` must divide the instance's base clock exactly

Not a board limitation so much as a trap this board fell into three times.
All three buses failed at bring-up and all three were blamed on the nRF54L15
datasheet's pin assignment table - P2.01 (SPIM00 SCK), P1.07 (SPIM21 SCK) and
P1.09 (TWIM22 SCL) were each read as not clock-capable, and each bus was
bit-banged over GPIO for a while as a result.

All three readings were wrong, and each failure had a mundane config cause:

| Bus | Real cause |
|---|---|
| SPIM21 | wrong NOR flash `jedec-id` |
| TWIM22 | wrong SHT40 address (0x45 assumed, 0x44 fitted) |
| SPIM00 | `spi-max-frequency = <10000000>` |

There is no pin-capability check in the nRF54L SPIM path at all. The only
`-EINVAL` `nrfx_spim_init()` can return comes from `spim_frequency_valid_check()`,
and the dedicated-pin check sitting next to it is guarded by
`p_reg == NRF_SPIM4` - an nRF52840/nRF5340 rule that cannot fire on this die.
The frequency check is pure arithmetic:

    prescaler = base_frequency / frequency
    valid  <=>  (base_frequency % frequency) < prescaler
                && prescaler is even
                && prescaler within the instance's divisor range

SPIM00's base clock is 128 MHz, so 10 MHz gives `prescaler = 12` and
`8000000 < 12` is false - rejected. 8 MHz gives `prescaler = 16`, exact and
even, and works. SPIM21's base is 16 MHz with its own divisors, which is why
the flash at 8 MHz passed the same check all along and made the failure look
bus-specific and therefore pin-shaped.

The reason this does not look like a config bug: on nRF52/nRF53,
`resolve_freq()` rounds a requested frequency down to the nearest supported
enum step, so `10000000` silently becomes 8 MHz and works. On nRF54L
(`NRF_SPIM_HAS_PRESCALER`) it returns the value unchanged, so it reaches nrfx
verbatim and hard-fails.

**Practical rule:** on SPIM00 use 32, 16, 8, 4, 2 or 1 MHz. Never copy a
`spi-max-frequency` between `&spi00` and `&spi21` without redoing the
arithmetic for the target instance's base clock.

### 3. Crystal load capacitors are unverified

`&lfxo` and `&hfxo` are configured for internal load capacitors using the
nRF54L15 DK values (17000 fF / 15000 fF). The schematic shows external trim
capacitors (0.5 pF, 0.75 pF, 3.9 pF) alongside Y1/Y2, which is consistent with
internal caps in use, but the values have **not** been verified against the
fitted crystals. Measure the frequency error and trim before release.

### 4. Pressure sensor is confirmed LPS22HB, not LPS22HH - resolved

Bring-up read WHO_AM_I `0xB1` off the real part, confirming an LPS22HB is
fitted (matching the schematic), not the LPS22HH the `st,lps22hh` Zephyr
driver requires (`0xB3`, or it refuses to bind). `CONFIG_LPS22HH` is off; the
`lps22` node is bound to `drivers/sensor/lps22hb_spi/`, a small local driver
that accepts either WHO_AM_I - see the LPS22HB/LPS22HH note in the top-level
README. The hardware-validation build was never affected either way; it
always talked to the part directly.

(The NOR flash `jedec-id` question - was `68 40 15`, assumed Boya BY25Q16BS -
is resolved: bring-up confirmed `68 10 15`, now reflected in the device tree
and in `test_flash_id()`.)

## Battery measurement

There is no divider on the PCB and none is required. The CR2032 feeds VCC
through a P-channel FET with no regulator, so the SoC supply rail is the cell
voltage, and the nRF54L15 SAADC measures it on the internal VDD channel
(NRF_SAADC_VDD). The channel is declared under /zephyr,user and read by
src/sensors/sensor_manager.c.

Do not use NRF_SAADC_AVDD here - on nRF54L15 that channel reads the internal
0.9 V analog supply rail (`SAADC_CH_PSELP_INTERNAL_Avdd` in the vendor
headers), not the battery. This was fitted originally and produced a
constant ~0.9 V reading regardless of actual cell voltage. NRF_SAADC_VBAT,
seen in `zephyr/dt-bindings/adc/nrf-saadc.h`, does not apply either - it is
a shim shared across the nRF54 family and this die's SAADC PSELP field only
defines Avdd/Dvdd/Vdd.

The 3.0 V to 2.0 V straight-line percentage map in sensor_manager.c is a
reasonable CR2032 approximation under a light pulsed load, but it has not been
checked against a real discharge curve for this design.
