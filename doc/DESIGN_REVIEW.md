# PLAN.md design review

This review looks at `PLAN.md` against the firmware that actually exists:
a dual-hardware Smart Tag (HOLyiot 25025 and Nordic nRF54L15 Tag / PCA20072)
that reports sensors over Zigbee and BLE.

`PLAN.md` is a strong V1 sketch. It is no longer the design of record. The
current architecture, requirements, phases and tests live in
[DESIGN.md](DESIGN.md).

## What still holds

The original plan got the hard product decisions right, and the firmware
followed them:

| PLAN.md idea | Verdict |
|---|---|
| Zigbee and BLE sit *above* a common application layer | Keep. Neither protocol owns sensors. |
| One `sensor_data_t` fanned out to both stacks | Keep. Subscriber pattern is the right seam. |
| BLE is local maintenance, not a second telemetry protocol | Keep. On-demand advertising is what makes a coin cell viable. |
| Zigbee is remote operation (sleepy ED + standard clusters + one custom cluster) | Keep. |
| Report-on-change, not every sample | Keep. `zigbee_reporting.c` is the policy module the plan asked for. |
| Interrupt-driven motion where the PCB allows it | Keep the *seam* (`motion_manager_process()` does not care what woke it). |
| Fixed-record circular logs, not a filesystem | Keep. |
| One product identity, several operating modes | Keep. Mode is runtime, not a build flavour. |
| Seven staged phases rather than a big-bang | Keep the idea; the contents of each phase need a dual-board rewrite. |

Do not collapse Zigbee and BLE into one "wireless" module. Do not put a
filesystem on the log partitions. Do not advertise BLE continuously.

## What the original plan did not know

`PLAN.md` describes a single board: SHT40 + LIS2DH12 + LPS22DP + 2 MB NOR.
The tree now supports two boards that share **no sensors**:

| | HOLyiot 25025 | nRF54L15 Tag (PCA20072) |
|---|---|---|
| Environment | SHT40 + LPS22HB | BME688 (T/RH/P/gas) |
| Motion | LIS2DH12, polled (INT on P2, no GPIOTE) | ADXL367 wake on P0.03 + BMI270 while moving |
| Logs | External BY25Q16 NOR, 2 MB | Internal RRAM (MX25R6435F not fitted) |
| Wake | RTC only | RTC + accelerometer activity |

Several PLAN.md sections are therefore underspecified or wrong as written:

1. **Section 3 sensor files.** `sht40.c` / `lis2dh12.c` / `lps22dp.c` would
   duplicate Zephyr drivers and would not compile on the Tag. The right
   split is a board sensor *backend*, not a per-chip register layer.
2. **Section 4 `sensor_data_t`.** Missing battery, gas, gyroscope, a validity
   mask, and a timestamp. Protocol v3 already added those; the plan did not.
3. **Section 9 flash map.** Configuration does not belong on NOR (4 KB erase
   for a 40-byte struct, and Bluetooth already needs internal settings).
   The Tag has no NOR at all.
4. **Section 10 interrupt map.** INT1/INT2 on P2 cannot wake nRF54L15. The
   plan treats interrupt-driven motion as a given; it is a *board
   capability*.
5. **Section 13 phases.** Phase 1 "all sensors ✓" is not one milestone. It
   is two hardware bring-ups with different pass criteria.
6. **LPS22DP vs LPS22HB.** The schematic and WHO_AM_I (`0xB1`) say LPS22HB.
   Zephyr's SPI `lps22hh` driver rejects that ID.

## Design gaps to close

These are the improvements that should land in the redefined design, roughly
in the order they hurt.

### 1. Board capability layer, not `#if HAS_BME688`

`HAS_BME688` is currently used as a proxy for "this is the Nordic Tag",
which also decides the accelerometer, the barometer, the wake source and
the flash. That coupling will break the moment a third board appears, or
the moment a Tag-like board has a BME688 and no BMI270.

Replace the identity test with explicit capabilities:

- `has_gas`, `has_imu`, `has_wake_source`, `has_ext_flash`
- selected from devicetree / Kconfig, not from one sensor alias

The application layer already does this well (`sensor_manager_expected_valid()`,
`sensor_manager_wake_source()`). Push the same idea down into the sensor
backends and board identity header.

### 2. Host-testable core

Motion classification, shock detection, reporting policy, config validation
and the flash slot format are pure functions sitting behind Zephyr headers.
`smart_tag.h` including `<zephyr/kernel.h>` and `<zephyr/devicetree.h>` is
what stops them being unit-tested on a laptop. Strip Zephyr out of the
wire types; keep DT in a board-identity header the firmware includes.

### 3. A real timebase

Every timestamp is `k_uptime_get() / 1000`. After a reboot a shipping audit
cannot tell when a shock happened. V1 can keep uptime for the live
protocol, but the design must reserve:

- a boot epoch (UTC seconds, 0 = unknown)
- `event_record_t` / `sensor_log_record_t` carrying uptime, with epoch
  stored once in statistics / a header record
- a BLE command and a Zigbee write to set the epoch (phone or gateway)

Without this, the logs are a debug ring, not evidence.

### 4. Config schema version and migration

`config_storage` rejects any blob whose size differs from the running
struct. That is safe and also discards operator settings on every protocol
bump (v2 → v3 already did this). Add a leading `uint16_t schema_version`
and an explicit migrate function. Missing fields take defaults; unknown
trailing bytes are ignored.

### 5. Battery alarm hysteresis

Temperature and humidity alarms unlatch when the reading returns inside
the window. Battery low latches forever. A recovering cell (or a sample
taken during a radio pulse) will keep `ALARM_BATTERY_LOW` up for the rest
of the deployment. Unlatch with hysteresis (~100 mV).

### 6. Config change fan-out lives in one place

`app_config_set()` updates `motion_manager` and persists. BLE then
separately pokes `power_manager`. Zigbee interval writes go through
`app_config_set_sampling_interval()` and would miss the sampling thread
and the LED enable flag. The application layer must own the fan-out:
thresholds, sampling interval, LED enable.

### 7. Sequence numbers on the wire

Flash slots have a sequence number. `event_record_t` and
`sensor_log_record_t` do not, and `flash_log_read()` does not return it.
A phone decoding Log Data chunks can lose its place after a drop and
cannot resume from a specific record except by guessing from
`start_seq`. Put `seq` in the chunk header per record, or in the record
itself.

### 8. Subscriber list is init-only — document it

`app_state_subscribe()` is safe today because both stacks register once
at boot. Iteration happens without the lock. Either freeze the list after
`RUNNING`, or iterate under the lock and snapshot callbacks. Do not
pretend it is a hot-plug bus.

### 9. Multiprotocol is a feature, not a footnote

PLAN.md section 12 mentions concurrent BLE + Zigbee and then the README
says Zigbee is compiled out and MPSL is "additionally needed". For a
product that "supports reporting via Zigbee and BLE", the design must
state:

- V1.0: BLE always; Zigbee opt-in via the R23 add-on
- V1.1: MPSL dynamic switching, radio policy (Zigbee long-poll vs BLE
  connection events), measured current
- What happens if both want the radio (BLE connection holds the window;
  Zigbee reports queue)

### 10. Production defaults vs bring-up defaults

`CONFIG_SMART_TAG_BLE_ADVERTISE_AT_BOOT=y` and Nordic's manufacturer
code `0x127F` / company ID `0xFFFF` are in the tree as if they were
product settings. They are not. Split `prj.conf` (product) from
`prj_debug.conf` (bring-up). Gate shipping on real IDs.

### 11. Watchdog, brown-out, image confirmation

None of these appear in PLAN.md. A tag that wedges on a sensor I2C NAK
will sit on a shelf until the cell dies. Phase 6/7 must include WDT,
low-voltage reset behaviour, and MCUboot confirm-or-revert after OTA.

### 12. HOLyiot motion is a hardware problem

Software cannot make P2 raise GPIOTE. Shipping-mode 4× polling is a
workaround, not a solution: a 15 s sample interval still misses a
forklift impact. The design should say clearly: interrupt-capable INT
routing is a V1.1 PCB requirement for the HOLyiot board; firmware will
not pretend otherwise.

## Suggested enhancements (not blockers)

- **Gas commissioning.** `gas_low_threshold_ohm = 0` is correct. Add a
  BLE command that snapshots the current resistance after burn-in and
  writes a floor at e.g. 70 % of that baseline, instead of leaving the
  operator to invent a number.
- **Orientation hysteresis.** Dominant-axis with a 0.5 g floor chatters
  when two axes are close. Require the winner to beat the runner-up by
  ~200 mg.
- **Shock peak during latch.** The detector already tracks peak; the
  event is raised on the *first* sample over threshold, so a 6 g impact
  that peaks on sample 2 is logged as 4 g. Raise the event on re-arm
  (end of impact) with the peak, or update the last event.
- **DIS serial number.** Derive from the BLE address or FICR so two tags
  are distinguishable in nRF Connect without a custom parser.
- **Log encryption / integrity beyond CRC.** CRC-16 catches torn writes,
  not tampering. A shipping tag that claims "no shock" needs at least a
  keyed MAC before anyone treats the NOR as an audit trail.
- **native_sim / QEMU later.** Host Unity tests cover policy. A Zephyr
  `native_sim` target would cover threading and settings without a DK.

## What not to change

- Do not add per-chip driver files in `src/sensors/`.
- Do not put config/statistics on the log flash.
- Do not make BLE a telemetry twin of Zigbee.
- Do not enable `CONFIG_PM` until the duty cycles are measured (Phase 6).
- Do not replace the subscriber pattern with direct `zigbee_*` /
  `ble_*` calls from `app_state.c`.
