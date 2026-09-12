# nRF54L15 Smart Tag

Starter firmware for two nRF54L15 boards that share an application and a
protocol, but no sensors. Design of record: [`doc/DESIGN.md`](doc/DESIGN.md).
Review of the original sketch: [`doc/DESIGN_REVIEW.md`](doc/DESIGN_REVIEW.md).

## Supported boards

| Target | Sensors | Log storage | Motion wake |
|---|---|---|---|
| `holyiot_25025/nrf54l15/cpuapp` | SHT40, LPS22HB, LIS2DH12 | external BY25Q16 NOR | no - polled |
| `nrf54l15tag/nrf54l15/cpuapp` | BME688, ADXL367, BMI270 | internal RRAM | yes - ADXL367 INT1 |

The two boards share no sensors at all. Everything above the sensor layer is
common. Board differences are confined to the overlay, `boards/<board>.conf`,
one sensor backend (`src/sensors/sensor_holyiot.c` or
`src/sensors/sensor_nrf54l15tag.c`), and one hardware-validation file.

The nRF54L15 Tag board definition ships with Zephyr
(`zephyr/boards/nordic/nrf54l15tag`), so only an application overlay is needed -
see `boards/nrf54l15tag_nrf54l15_cpuapp.overlay`. Note that several parts on
that board are depopulated per its BOM (external flash, second RGB LED, second
and third buttons, buzzer, light sensor); none of them are described in
devicetree or used in code.

## Hardware verified from schematic (HOLyiot 25025)

MCU: nRF54L15-SOC1-QFAA-AB0

| Function | MCU pin | Bus |
|---|---|---|
| SHT40 SCL | P1.09 | TWIM22 |
| SHT40 SDA | P1.11 | TWIM22 |
| Sensor SPI SCK | P2.01 | SPIM00 |
| Sensor SPI MOSI / SDI | P2.02 | SPIM00 |
| Sensor SPI MISO / SDO | P2.04 | SPIM00 |
| LIS2DH12 CS | P2.05 | GPIO |
| LIS2DH12 INT2 | P2.03 | GPIO |
| LIS2DH12 INT1 | P2.00 | GPIO |
| LPS22HB CS | P2.06 | GPIO |
| RGB LED B | P2.07 | GPIO |
| RGB LED G | P1.10 | GPIO |
| RGB LED R | P2.09 | GPIO |
| User button | P1.13 | GPIO |
| SPI NOR CS | P1.04 | GPIO |
| SPI NOR SO (MCU MISO) | P1.05 | SPIM21 |
| SPI NOR SI (MCU MOSI) | P1.06 | SPIM21 |
| SPI NOR SCLK | P1.07 | SPIM21 |
| SWDIO | P0.01 | SWD |
| SWCLK | P0.00 | SWD |
| RESET | P0.04 | SWD |

## Bus topology

The external NOR flash is **not** on the sensor SPI bus. The schematic gives it
its own SCLK/SI/SO on P1.07/P1.06/P1.05.

All three buses run as real hardware peripherals. That was not the original
conclusion: per the nRF54L15 datasheet's pin assignment table, P2.01 (SPIM00
SCK), P1.07 (SPIM21 SCK) and P1.09 (TWIM22 SCL) all looked like they weren't
clock-capable, and all three buses were bit-banged over GPIO for a while as a
result.

All three readings were wrong. There is no pin-capability check in the nRF54L
SPIM path at all - `nrfx_spim_init()`'s only `-EINVAL` is a frequency check.
Each bus had a mundane config bug: a wrong NOR flash `jedec-id` (SPIM21), a
wrong SHT40 address (TWIM22), and a `spi-max-frequency` of 10 MHz (SPIM00),
which SPIM00's 128 MHz base clock cannot divide down to. At 8 MHz the sensor
bus initialises and both parts answer, confirmed on hardware. See the board
README's limitation #2 for the arithmetic and the rule it implies.

| Bus (devicetree node) | Pins | Devices |
|---|---|---|
| SPIM00 (hardware) | P2.01/02/04 | LIS2DH12 (CS P2.05), LPS22 (CS P2.06) |
| SPIM21 (hardware) | P1.07/06/05 | BY25Q16BSXIG NOR (CS P1.04) |
| TWIM22 (hardware) | P1.09/11 | SHT40 |

`spi-max-frequency` is 8 MHz on all three sensor/flash nodes, but for two
different reasons - SPIM00 divides a 128 MHz base clock, SPIM21 a 16 MHz one.
The values are not interchangeable between the instances; changing either
means redoing the prescaler arithmetic for that instance.

## Hardware limitations to know about

### Accelerometer interrupts cannot wake the SoC (HOLyiot 25025 only)

LIS2DH12 INT1/INT2 are wired to P2.00/P2.03. Port P2 on nRF54L15 has **no
GPIOTE and no SENSE/DETECT mechanism**, so those lines cannot raise a GPIO
interrupt and cannot wake the device from sleep.

That means `CONFIG_LIS2DH_TRIGGER_GLOBAL_THREAD` / `_OWN_THREAD` must stay off
(`gpio_pin_interrupt_configure()` returns `-ENOTSUP` and the driver's init
fails), and motion detection has to be polled on this board revision. There,
the sampling interval also sets the motion latency, which is why the operating
mode scales it.

**This does not apply to the nRF54L15 Tag.** Its ADXL367 INT1 is on P0.03, and
port P0 has GPIOTE30 and supports pin sense. The application arms an activity
trigger on it, so the sampling thread wakes on movement in milliseconds rather
than at the next tick, and a stationary tag can sleep at the plain configured
interval. `sensor_manager_wake_source()` returns the wake device, or NULL on a
board that has none, and `power_manager.c` is written to work either way.

### `spi-max-frequency` is not rounded on nRF54L

On nRF52/nRF53 an unsupported SPI frequency is silently rounded down to the
nearest supported step. On nRF54L it is passed to nrfx verbatim and rejected
with `-EINVAL` unless it divides the instance's base clock exactly into an
even prescaler. This cost this project three bit-banged buses and two
rewrites of the pin documentation before it was found - see the board
README's limitation #2.

## Sensor drivers

Every sensor is driven through the Zephyr sensor API and reached by devicetree
alias, so `sensor_manager.c` names no bus and no part outside its own private
`#if` blocks. Which drivers are built is a per-board `.conf` matter.

HOLyiot 25025 - two in-tree drivers and one small local one, see the
LPS22HB/LPS22HH caveat below:

| Part | DT compatible | Driver |
|---|---|---|
| SHT40 | `sensirion,sht4x` | In-tree, `CONFIG_SHT4X` |
| LIS2DH12 | `st,lis2dh` | In-tree, `CONFIG_LIS2DH` |
| LPS22HB | `holyiot,lps22hb` | Local, `CONFIG_LPS22HB_SPI` - `drivers/sensor/lps22hb_spi/` |

nRF54L15 Tag - all in-tree:

| Part | DT compatible | Driver |
|---|---|---|
| BME688 | `bosch,bme680` | In-tree, `CONFIG_BME680` |
| ADXL367 | `adi,adxl367` | In-tree, `CONFIG_ADXL367` (+ trigger) |
| BMI270 | `bosch,bmi270` | In-tree, `CONFIG_BMI270` |

Two structural differences on the Tag:

- The BME688 answers temperature, humidity, pressure **and gas resistance**
  from one fetch, where the HOLyiot board needs two devices for the first
  three and has no gas sensor. One device therefore carries three of the
  `SENSOR_VALID_*` bits.
- The BMI270 is read **only while the tag is moving**. The ADXL367 is the
  cheap always-on sensor and the wake source; spinning up a 6-axis IMU to
  measure a gyroscope on a shelf is not worth the battery. A stationary sample
  legitimately has `SENSOR_VALID_GYRO` clear, which is why the IMU is excluded
  from `sensor_manager_expected_valid()`.

The RGB LED is common-anode on both boards, therefore LED GPIOs are active-low.
Only LED1 is fitted on the Tag; `led-red`/`led-green`/`led-blue` alias its three
channels.

### LPS22HB / LPS22HH caveat - resolved with a local driver

The schematic calls the pressure sensor **LPS22HB**, not LPS22DP. Confirmed
at bring-up: WHO_AM_I on CS P2.06 reads `0xB1`, an actual LPS22HB, not an
LPS22HH.

Zephyr has an `st,lps22hb` driver, but it is **I2C only** - the part is
wired in SPI mode on this PCB. The closest SPI-capable in-tree driver is
`st,lps22hh`, whose `lps22hh_init_chip()` hard-rejects any WHO_AM_I other
than `0xB3` and refuses to bind.

Rather than patching that driver in place, `drivers/sensor/lps22hb_spi/` is
a small local out-of-tree driver bound to the `holyiot,lps22hb` compatible
(`CONFIG_LPS22HB_SPI`, `CONFIG_LPS22HH` stays off - see `prj.conf`). It's a
trimmed copy of the in-tree driver's approach without ST's HAL dependency or
trigger support (nothing on this board wires the interrupt line anyway) -
same CTRL_REG1 ODR/BDU bits, same 24-bit `PRESS_OUT` registers, same
`SENSOR_CHAN_PRESS` conversion, just accepting either `0xB1` or `0xB3` at
`WHO_AM_I`. `sensor_manager.c` is unaware of any of this; it just calls
`sensor_sample_fetch()`/`sensor_channel_get()` on `dev_baro` like any other
sensor.

The SHT40 part number is SHT40-AD1B-R2. The datasheet's "B" address variant
would be I2C address 0x45, but bring-up (logic analyzer) found the fitted
part answers on the default address, 0x44 - see the board `.dts`.

No battery divider is present in the schematic, and none is needed: the
CR2032 feeds VCC through a P-channel FET with no regulator, so the SoC supply
rail is the cell voltage and the SAADC reads it on its internal VDD channel.
See "Sensors and battery" below.

## Application architecture

Implements the module tree from [`doc/DESIGN.md`](doc/DESIGN.md) section 5
(originally `PLAN.md` section 3). Zigbee and BLE sit above a common
application layer; neither owns the sensors.

    src/
      app/
        app_main.c         startup, button, state sequencing
        app_state.c/h      state machine, live status, fan-out to protocols
        app_config.c/h     configuration validation and effective values
        app_alarms.c/h     threshold latching (host-testable)
        smart_tag.h        shared wire types (host-testable)
        board_id.h         per-board model string (device tree)
      sensors/
        sensor_manager.c/h public API, battery, magnitude
        sensor_board.h     board backend interface
        sensor_holyiot.c   SHT40 / LPS22HB / LIS2DH12
        sensor_nrf54l15tag.c BME688 / ADXL367 / BMI270
      motion/
        motion_manager.c/h STATIONARY <-> MOVING state machine, tamper
        motion_classifier.c/h  magnitude, activity, orientation
        shock_detector.c/h impulse detection with re-arm hysteresis
      storage/
        flash_manager.c/h  fixed-record circular logger
        event_log.c/h      event records
        sensor_log.c/h     sensor history records
        config_storage.c/h configuration and statistics persistence
        config_migrate.c/h NVS schema header + v3 blob migrate
      ui/
        led_manager.c/h    priority-based RGB indications
      power/
        power_manager.c/h  sampling duty cycle, BLE service window
        watchdog.c/h       pause-in-sleep SoC watchdog
      ble/
        ble_manager.c/h    stack and advertising lifecycle
        ble_gatt.c/h       service table and notifications
        ble_config.c/h     configuration and command characteristics
        ble_log.c/h        chunked log download
      zigbee/
        zigbee_manager.c/h stack lifecycle, commissioning, signals
        zigbee_clusters.c/h  ZCL attribute storage and writes
        zigbee_reporting.c/h report-on-change policy
        zigbee_ota.c/h     OTA via the add-on FOTA library
        zigbee_tag_cluster.h endpoint and Tag Monitor cluster declarations

The data flow is the one in `PLAN.md` section 3. `power_manager` wakes,
`sensor_manager_read()` fills a `sensor_data_t`, and `app_process_sensor_data()`
publishes it:

                        sensor_data_t
                             |
                  +----------+----------+
                  |                     |
            Zigbee reporter        BLE GATT
                  |                     |
           ZCL attributes       characteristics

Protocol stacks register an `app_subscriber` rather than being called by name,
so the application layer names neither of them.

### Two deliberate departures from PLAN.md

**Per-chip driver files.** `PLAN.md` sketches `sensors/sht40.c`, `lis2dh12.c`
and `lps22dp.c`. Zephyr already has in-tree drivers for all three parts, bound
from the device tree, so hand-written register layers would be duplicated code
with no owner. `sensor_manager.c` is the thin wrapper the plan asks for; the
bindings live in the board device tree.

**Configuration in flash.** `PLAN.md` section 9 places configuration and
statistics in the external NOR with the logs. They live in the internal RRAM
settings partition instead: the Bluetooth stack already requires settings on
internal flash for bonding keys, and a small read-modify-write config does not
belong in a partition whose erase unit is 4 KB. The NOR carries the two logs,
which is where its capacity is actually needed.

## Operating modes

`PLAN.md` section 14: one product, one firmware, several roles. The mode is a
runtime setting, not a build option, and scales the sampling rate.

| Mode | Sampling | Emphasis |
|---|---|---|
| Environment (0) | configured interval | climate history |
| Asset (1) | 2x faster | presence, tamper |
| Shipping (2) | 4x faster | shock, free fall |

## Sensors and battery

`sensor_manager` produces the `sensor_data_t` of `PLAN.md` section 4, extended
with battery fields because section 2 lists them as a V1 requirement.

Battery voltage was initially assessed as unimplementable here, because the
schematic has no divider. It turns out none is needed: the CR2032 feeds VCC
through a P-channel FET with no regulator, so the SoC supply rail is the cell
voltage, and the nRF54L15 SAADC can measure VDD internally (`NRF_SAADC_VDD`).
Percentage is a straight-line map from 3.0 V to 2.0 V, adequate for a coin
cell under a light pulsed load.

`NRF_SAADC_AVDD` reads the internal 0.9 V analog supply rail instead, not the
battery - an earlier revision of this board file used it by mistake and read
a constant ~0.9 V regardless of actual cell voltage.

## Motion detection

`PLAN.md` section 10 asks for interrupt-driven detection with INT1 carrying
motion, inactivity and orientation, and INT2 carrying free fall and shock.
**That is not possible on Beacon V1.0** - see the hardware limitations above.
Both lines land on port P2, which has no GPIOTE and no SENSE/DETECT.

The state machine is therefore fed from polled samples, and the sampling
interval doubles as the motion detection latency. The seam is deliberate:
`motion_manager_process()` does not care where its sample came from, so a board
revision that routes INT1 to port P1 only has to call it from an interrupt
handler instead of the sampling thread.

Tamper has no switch on this PCB either, so it is inferred: a tag that has been
undisturbed for five minutes, then takes a shock and ends up facing a different
way, is reported as tampered. Requiring all three conditions keeps ordinary
handling noise from raising it.

## RGB LED

Colour meanings are fixed by `PLAN.md` section 2. Several indications can be
true at once, so `led_manager` keeps a set and shows the highest priority
rather than letting the last caller win. Every pattern is a short blink on a
long period, because a coin cell cannot run an LED continuously.

| Colour | Meaning | Priority |
|---|---|---|
| Blue | BLE advertising | lowest |
| Yellow | Zigbee joining | |
| Green | Zigbee connected | |
| Purple | OTA in progress | |
| Red | alarm | |
| White | identify | highest |

## Storage

Two independent circular logs, so a burst of shock events cannot evict the
climate history a shipping audit needs. Where they live is a board property:

HOLyiot 25025 - external BY25Q16 NOR (2 MB):

| Partition | Size | Record | Capacity |
|---|---|---|---|
| `event-log` | 256 KB | `event_record_t` | ~8 200 events |
| `sensor-history` | 1792 KB | `sensor_log_record_t` | ~57 000 samples |

nRF54L15 Tag - internal RRAM, since its MX25R6435F footprint is depopulated
(BOM U8 = Not Fitted):

| Partition | Offset | Size | Capacity |
|---|---|---|---|
| `event-log` | 0x100000 | 64 KB | ~2 000 events |
| `sensor-history` | 0x110000 | 400 KB | ~12 800 samples |

That space comes from cutting both MCUboot image slots from 712 KB to 480 KB,
which the ~200 KB application leaves ample room for. `slot1` is kept rather
than reclaimed, because `zigbee_ota.c` and `sysbuild.conf` both anticipate an
OTA slot. `storage_partition` deliberately keeps its upstream address, so
settings already saved on a device survive the change.

`flash_manager` needed no porting for RRAM: it writes whole 32-byte slots at
32-byte-aligned offsets and erases whole sectors, and RRAM's 16-byte write
block and 4096-byte erase block both divide that cleanly.

Both use `flash_manager`, the fixed-record engine from `PLAN.md` section 9 with
append, read, erase and get_oldest. Slots are 32 bytes with a magic and a
CRC-16 over the sequence number and payload, so a torn write is skipped at the
next boot with no metadata to repair. When the ring wraps, the sector about to
be reused is erased - the oldest sector is dropped, not the oldest record.
Sequence numbers are contiguous, so a read seeks directly to a record instead
of scanning the partition.

## BLE

`PLAN.md` section 7: BLE is local maintenance, not a second telemetry protocol.
Section 11: it does not advertise continuously. A short button press opens a
service window (120 s by default); the window closes on timeout, and is held
open while a transfer is in progress.

Device Information comes from the standard SIG service (0x180A) so generic
tools show it without knowing the product. The rest is one custom service,
`f0d1a000-9e4b-4b7a-9c2e-2a5b1d250250`:

| Characteristic | UUID suffix | Properties | Payload |
|---|---|---|---|
| Sensor Data | `f0d1a001` | read, notify | `sensor_data_t` |
| Status | `f0d1a002` | read, notify | `tag_status_t` |
| Configuration | `f0d1a003` | read, write | `tag_config_t` |
| Event | `f0d1a004` | read, notify | `event_record_t` |
| Log Information | `f0d1a005` | read | `struct ble_log_info` |
| Log Control | `f0d1a006` | write | `struct ble_log_request` |
| Log Data | `f0d1a007` | notify | chunked records |
| Command | `f0d1a008` | write, notify | request / response |
| Statistics | `f0d1a009` | read | `tag_statistics_t` |

`PLAN.md` section 8 lists the characteristics as individual fields. They are
grouped one struct per heading here, because each extra characteristic is
another ATT round trip and radio time is the dominant cost on a coin cell.

### Protocol version 4

`SMART_TAG_PROTOCOL_VERSION` is 4. A client written against v3 will
misread the log-chunk header and the statistics characteristic, so check
the version byte in the advertising manufacturer data before decoding.
What changed from v3:

- **Log Data chunk header** gained `uint32_t first_seq` (sequence of the
  first record in the payload). Size is now 10 bytes.
- **`tag_statistics_t`** replaced the trailing reserved `uint16` with
  `boot_epoch_utc` (UTC seconds at boot; 0 = unknown). Size is now 42
  bytes. Wall-clock of a log record is `boot_epoch_utc + timestamp`.
- **Commands** 0x0B `SET_TIME` (argument = current UTC seconds) and 0x0C
  `SNAPSHOT_GAS_BASELINE` (sets `gas_low_threshold_ohm` to 70 % of the
  last valid gas reading).
- **NVS** config/stats are stored with a `{schema, body_size}` header.
  A raw 40-byte protocol-v3 blob still loads.

v3 still applies for the live sample:

- **`sensor_data_t` is `__packed`.**
- **Gas resistance** and **gyroscope** fields and validity bits.
- **`ALARM_GAS_LOW`** / **`EVENT_GAS_LOW`**.

On the HOLyiot board the gas/gyro fields are always zero and their
validity bits always clear, so one decoder handles both boards.

Log download is the chunked protocol: write a request to Log Control,
receive numbered chunks on Log Data, with `first_seq` on each header and
the last chunk flagged so a client can tell completion from a stall.

    Phone                        Tag
      |-- LogControl(START) ----->|
      |<--- LogData chunk 0 ------|
      |<--- LogData chunk 1 ------|
      |             ...           |
      |<--- LogData chunk N ------|  FLAG_LAST

Commands: 0x01 Identify, 0x02 and 0x03 clear the event and sensor logs,
0x04 reset counters, 0x05 factory reset, 0x06 sample now, 0x07 set mode,
0x08 clear alarms, 0x09 reboot, 0x0A start calibration (`-ENOTSUP`),
0x0B set time, 0x0C snapshot gas baseline.

Production builds leave BLE off until a button press. For nRF Connect
bring-up without the button:

    west build -b holyiot_25025/nrf54l15/cpuapp -- -DEXTRA_CONF_FILE=prj_debug.conf

## Zigbee

Endpoint 1, sleepy end device, 15 s long poll, exactly the cluster set from
`PLAN.md` section 5:

| Cluster | Role | Notes |
|---|---|---|
| Basic | server | manufacturer, model, date code |
| Identify | server + client | client role makes the tag a finding and binding target |
| Power Configuration | server | battery voltage and percentage |
| Temperature Measurement | server | 0.01 degrees C |
| Relative Humidity Measurement | server | 0.01 %RH |
| Pressure Measurement | server | kPa |
| Tag Monitor 0xFC00 | server | manufacturer specific |

Tag Monitor carries attributes 0x0000 to 0x000C exactly as listed in section 5,
so a gateway's generic cluster parser can consume them unchanged. OperatingMode
and SamplingInterval are writable. OTA Upgrade lives on its own endpoint, owned
by the add-on's `zigbee_fota` library.

### Reporting policy

`PLAN.md` section 6: do not report every measurement. `zigbee_reporting.c`
compares each sample against what was last sent.

| Attribute | Reported when |
|---|---|
| Temperature | changed by 0.5 C or more, or 30 min elapsed |
| Humidity | changed by 3 %RH or more, or 30 min elapsed |
| Pressure | changed by 1 hPa or more, or 30 min elapsed |
| Battery | changed by 1 % or more, or 30 min elapsed |
| Motion, shock, free fall, tamper | immediately |

All four thresholds and the maximum interval are configurable at runtime.

### Zigbee is not enabled by default

The Nordic Zigbee R23 add-on is a separate repository and is not in the
workspace. Every file under `src/zigbee/` compiles to a stub unless
`CONFIG_ZIGBEE_ADD_ON` is set, so the default build does not need it. To turn
Zigbee on:

1. Uncomment the `ncs-zigbee` project in `west.yml` and run `west update`.
2. Uncomment the `CONFIG_ZIGBEE_*` options at the bottom of `prj.conf`.
3. Set a real manufacturer code in `src/zigbee/zigbee_tag_cluster.h`
   (`SMART_TAG_MANUF_CODE` is currently Nordic's 0x127F) and a real Bluetooth
   SIG company ID in `src/ble/ble_manager.c` (`COMPANY_ID`).

Running BLE and Zigbee simultaneously is a multiprotocol configuration and
additionally needs MPSL dynamic radio switching.

## Recommended SDK

Use nRF Connect SDK v3.4.0 LTS. It is based on Zephyr 4.4.

The Zigbee R23 add-on is installed separately. The current add-on release at the time this project was created is v1.3.0.

## Building

### Hardware validation first

PLAN.md phase 1. On a new board, run the peripheral test suite before the
application - it has no protocol stack in it, so a failure can only be
hardware, pinctrl or the device tree:

    west build -b holyiot_25025/nrf54l15/cpuapp -p always . \
        -- -DCONF_FILE=prj_validation.conf
    west flash

or, for the Nordic Tag:

    west build -b nrf54l15tag/nrf54l15/cpuapp -p always . \
        -- -DCONF_FILE=prj_validation.conf

The suite is split: `hw_validation.c` is the harness plus the tests that are
the same everywhere (LEDs, button, battery, log storage), and one
`hw_validation_<board>.c` provides the bus and sensor tests. CMake picks the
right one from the devicetree, not from Kconfig - this build deliberately
enables no sensor drivers, so `CONFIG_BME680` and friends are always `n` here.

On the HOLyiot board it settles which sensor is on which chip select, whether
the pressure sensor is an LPS22HB or an LPS22HH, and the NOR flash JEDEC ID. On
the Tag it checks the BME688, ADXL367 and BMI270 chip IDs and, most usefully,
that the ADXL367's INT1 really reaches P0.03 and that the pin can raise an
interrupt. See HARDWARE_VALIDATION.md.

Note that `-DCONF_FILE=...` makes Zephyr skip `boards/<board>.conf` entirely,
so the per-board sensor configuration used by the application build is not
applied to the validation build. That is intended.

### The application

The HOLyiot product has its own board definition, `holyiot_25025`, in
`boards/holyiot/holyiot_25025/`. Zephyr adds the application directory to
`BOARD_ROOT` automatically, so no extra arguments are needed:

    west build -b holyiot_25025/nrf54l15/cpuapp -p always .
    west flash

The Nordic Tag's board definition ships with Zephyr, so that target needs only
the overlay and `.conf` in `boards/`:

    west build -b nrf54l15tag/nrf54l15/cpuapp -p always .
    west flash

The out-of-tree vendor prefix is declared in `dts/bindings/vendor-prefixes.txt`.

There is no UART on the product, so the console comes out over SEGGER RTT:

    JLinkRTTViewer

### Building on an nRF54L15 DK

`boards/nrf54l15dk_nrf54l15_cpuapp.overlay` applies the same pin map on top of
the Nordic DK:

    west build -b nrf54l15dk/nrf54l15/cpuapp -p always .

None of the Smart Tag peripherals are fitted on the DK, so that build only
exercises the firmware structure, BLE, and the build itself.

## Host tests and CI

Policy modules (motion classifier, shock detector, motion manager, Zigbee
reporting, config validation, alarm policy, flash logger, config migrate,
wire types) are unit-tested on the host with the Unity framework. No nRF
Connect SDK is required:

    make -C tests/unit test
    make -C tests/unit coverage
    make -C tests/unit cppcheck

See [`tests/README.md`](tests/README.md). GitHub Actions runs the same three
commands on every push and pull request (`.github/workflows/ci.yml`). A full
`west build` of NCS is not in the PR gate.

## Suggested next steps

Mapped onto the phases in [`doc/DESIGN.md`](doc/DESIGN.md) section 12.
`PLAN.md` section 13 is the original single-board sketch.

**Phase 0, host quality gate** - Unity tests, coverage, cppcheck. In tree.

**Phase 1, hardware bring-up** - the validation firmware is written and ready
to run; nothing has been on hardware yet. Build it with
`-DCONF_FILE=prj_validation.conf` and work through HARDWARE_VALIDATION.md. It
answers the chip-select, pressure-sensor-identity and flash-ID questions, and
checks the battery reading, before any stack is layered on. Run it on *both*
boards.

**Phase 2, BLE** - complete: on-demand advertising, GATT service,
configuration, chunked log download.

**Phase 3, flash logger** - complete in firmware; host tests cover CRC skip
and wrap. Still to verify on hardware: power-loss recovery (pull power
mid-write and check the torn record is skipped) and wear behaviour over a
full wrap of each partition.

**Phase 4, Zigbee** - written against the R23 add-on API but never compiled;
the add-on is not in the workspace. Install it and build.

**Phase 4b, multiprotocol** - MPSL dynamic switching. Not started.

**Phase 5, motion** - complete as polled detection on Holyiot, activity-wake
on the Tag. A Holyiot INT respin is a hardware project (DESIGN.md §3.3).

**Phase 6, low power** - watchdog is in (pause-in-sleep, 8 s CPU window).
Enable `CONFIG_PM` and `CONFIG_PM_DEVICE`, measure the sampling and radio
duty cycles, and tune the defaults.

**Phase 7, OTA and production** - `zigbee_ota.c` is wired to the FOTA library,
but image signing, MCUboot/sysbuild partitioning, secure boot, factory
provisioning and production test remain. Allocate a real Zigbee manufacturer
code and Bluetooth SIG company ID before any of it ships. Production
`prj.conf` no longer advertises at boot; use `prj_debug.conf` for bring-up.

