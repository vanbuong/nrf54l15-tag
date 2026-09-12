> **This file is the original V1 sketch.** It is kept as history.
> The design of record is [DESIGN.md](DESIGN.md).
> Review of this sketch against the dual-hardware firmware:
> [DESIGN_REVIEW.md](DESIGN_REVIEW.md).
>
> Do not treat the sensor list, flash map, or interrupt plan below as
> authoritative — both boards, protocol v4, and the capability model
> live in DESIGN.md.

1. Target architecture
                         Smart Tag
                            │
             ┌──────────────┴──────────────┐
             │                             │
          Zigbee                          BLE
             │                             │
       Normal operation             Local operation
             │                             │
     ┌───────┴────────┐          ┌─────────┴─────────┐
     │                │          │                   │
  Sensors         Events     Commissioning       Diagnostics
  Reporting       Alarms     Configuration       Log download
     │                │          │                   │
     └───────┬────────┘          └─────────┬─────────┘
             │                             │
             └──────────────┬──────────────┘
                            │
                       Application
                            │
       ┌────────────────────┼────────────────────┐
       │                    │                    │
 Sensor Manager       Event Manager        Data Logger
       │                    │                    │
 ┌─────┼──────┐       ┌─────┼──────┐             │
 │     │      │       │     │      │             │
SHT40 LIS2DH12 LPS22DP Motion Shock Freefall   2MB NOR

I'd deliberately keep Zigbee and BLE above a common application layer. Don't let the Zigbee code directly own sensors.

2. What the first version should do

I'd define V1 as:

Environmental
Temperature
Relative humidity
Atmospheric pressure
Battery voltage
Battery percentage
Motion
Motion / stationary
Orientation
Free fall
Shock
Movement duration
Tamper/removal
Storage
Sensor history
Event history
Configuration
Device statistics
Zigbee
Join network
Sleepy End Device
Temperature Measurement
Humidity Measurement
Pressure Measurement
Power Configuration
Identify
OTA
Custom Tag cluster
BLE
Device discovery
Configuration
Sensor readout
Live sensor data
Event history download
Device diagnostics
Factory/service mode
RGB LED
Blue    = BLE advertising
Green   = Zigbee connected
Yellow  = Zigbee joining
Purple  = OTA
Red     = alarm
White   = identify
3. Firmware architecture

Since you're already working heavily with ESP-IDF/FreeRTOS, don't make the nRF firmware one giant Zigbee application.

I'd structure it like:

smart_tag/
│
├── app/
│   ├── app_main.c
│   ├── app_state.c
│   └── app_config.c
│
├── sensors/
│   ├── sensor_manager.c
│   ├── sht40.c
│   ├── lis2dh12.c
│   └── lps22dp.c
│
├── motion/
│   ├── motion_manager.c
│   ├── motion_classifier.c
│   └── shock_detector.c
│
├── storage/
│   ├── flash_manager.c
│   ├── sensor_log.c
│   ├── event_log.c
│   └── config_storage.c
│
├── zigbee/
│   ├── zigbee_manager.c
│   ├── zigbee_clusters.c
│   ├── zigbee_reporting.c
│   ├── zigbee_ota.c
│   └── zigbee_tag_cluster.c
│
├── ble/
│   ├── ble_manager.c
│   ├── ble_gatt.c
│   ├── ble_config.c
│   └── ble_log.c
│
├── ui/
│   └── led_manager.c
│
└── power/
    └── power_manager.c

The central application should see neither Zigbee nor BLE details.

For example:

sensor_data_t data;

sensor_manager_read(&data);

app_process_sensor_data(&data);

Then:

                    sensor_data_t
                         │
             ┌───────────┴───────────┐
             │                       │
       Zigbee reporter          BLE GATT
             │                       │
       Zigbee attributes       BLE characteristics

This will make the firmware much easier to maintain.

4. Sensor abstraction

I'd define a common structure:

typedef struct {
    int16_t temperature_c_x100;
    uint16_t humidity_x100;
    int32_t pressure_pa;

    int16_t accel_x_mg;
    int16_t accel_y_mg;
    int16_t accel_z_mg;

    bool motion;
    bool free_fall;
    bool shock;

    uint8_t orientation;
} sensor_data_t;

But don't necessarily read everything every time.

For example:

Normal environmental cycle:

wake
  ↓
SHT40
  ↓
LPS22DP
  ↓
store/report if necessary
  ↓
sleep

Whereas the LIS2DH12 can stay in its low-power interrupt mode:

             LIS2DH12
                 │
                 │ interrupt
                 ▼
             nRF54L15
                 │
           motion detected
                 │
        ┌────────┴────────┐
        │                 │
   record event       Zigbee report

That's the key to making this a battery-powered tag rather than a tiny continuously running sensor node.

5. Zigbee device model

I would use one primary endpoint:

Endpoint 1
│
├── Basic
├── Identify
├── Power Configuration
├── Temperature Measurement
├── Relative Humidity Measurement
├── Pressure Measurement
├── OTA Upgrade
└── Tag Monitor Cluster

The standard clusters should contain standard measurements.

Then make a manufacturer-specific cluster for the things that don't fit standard ZCL.

For example:

Tag Monitor Cluster

Attributes:

0x0000 MotionState
0x0001 Orientation
0x0002 ShockCount
0x0003 MaximumShock
0x0004 FreeFallCount
0x0005 TamperState
0x0006 MovementDuration
0x0007 LastMovementTime
0x0008 EventCount
0x0009 LogRecordCount
0x000A AlarmFlags
0x000B OperatingMode
0x000C SamplingInterval

This is especially convenient for your existing Zigbee gateway because your Zigbee Manager can expose these attributes through its generic cluster parser.

6. Zigbee reporting strategy

Don't report every measurement.

For example:

Temperature
  report if change >= 0.5 °C
  OR 30 minutes elapsed

Humidity
  report if change >= 3 %
  OR 30 minutes elapsed

Pressure
  report if change >= 1 hPa
  OR 30 minutes elapsed

Motion
  report immediately

Shock
  report immediately

Free fall
  report immediately

Tamper
  report immediately

So the normal sequence becomes:

                 SLEEP
                   │
                   │ RTC
                   ▼
              Read sensors
                   │
                   ▼
             Is value changed?
              /           \
            no             yes
            │               │
            │               ▼
            │         Zigbee report
            │               │
            └───────┬───────┘
                    ▼
                  SLEEP
7. BLE should NOT duplicate Zigbee

This is important.

I wouldn't make BLE another complete IoT protocol.

Instead:

Zigbee = remote operation
Hub
 │
 ├── temperature
 ├── humidity
 ├── pressure
 ├── motion
 ├── battery
 └── alarms
BLE = local maintenance
Phone
 │
 ├── Device information
 ├── Configuration
 ├── Current sensors
 ├── Event history
 ├── Sensor history
 ├── Diagnostics
 └── Factory test

That gives each protocol a clear purpose.

8. BLE GATT design

I'd create a custom service:

Smart Tag Service
UUID: your-company-specific

Characteristics:

Device Information
------------------
Firmware Version
Hardware Version
Serial Number
Device ID


Sensor
------
Temperature
Humidity
Pressure
Battery
Motion
Orientation


Configuration
-------------
Sampling Interval
Temperature Threshold
Humidity Threshold
Shock Threshold
Motion Timeout


Event
-----
Event Count
Latest Event
Event Control


Log
---
Log Information
Log Data
Log Control


Command
-------
Identify
Clear Log
Factory Reset
Start Calibration

For log download, don't try to put the entire history into one GATT characteristic.

Use a chunked protocol:

Phone

GET_LOG
   ↓
Tag

CHUNK 0
   ↓
CHUNK 1
   ↓
CHUNK 2
   ↓
...
   ↓
CHUNK N

You can eventually build a small Android/iOS/PC application around this.

9. 2 MB flash

I'd make the flash a circular event/data logger.

Something like:

2 MB SPI NOR
┌───────────────────────────┐
│ Configuration             │
├───────────────────────────┤
│ Device statistics         │
├───────────────────────────┤
│ Event log                 │
├───────────────────────────┤
│ Sensor history            │
│                           │
│       circular buffer     │
│                           │
└───────────────────────────┘

Don't use a filesystem initially.

For this application, I'd strongly prefer:

flash_manager
      │
      ├── append()
      ├── read()
      ├── erase()
      └── get_oldest()

with fixed-size records.

For example:

typedef struct {
    uint32_t timestamp;
    int16_t temperature;
    uint16_t humidity;
    int32_t pressure;
    uint8_t flags;
} sensor_log_record_t;

And a separate event record:

typedef struct {
    uint32_t timestamp;
    uint8_t type;
    uint8_t severity;
    int16_t value;
} event_record_t;

This is much more robust than putting a filesystem there initially.

10. Motion detection

I'd make the LIS2DH12 interrupt-driven.

Configure hardware interrupts for:

INT1
 ├── motion
 ├── inactivity
 └── orientation

INT2
 ├── free fall
 └── shock

The exact mapping depends on how you wire the interrupt pins.

Then the nRF doesn't have to periodically poll the accelerometer.

A useful state machine:

             ┌─────────────┐
             │   STATIONARY│
             └──────┬──────┘
                    motion
                      │
                      ▼
             ┌─────────────┐
             │    MOVING   │
             └──────┬──────┘
                    │
             timeout expired
                    │
                    ▼
             ┌─────────────┐
             │   STATIONARY│
             └─────────────┘

Meanwhile:

free fall ────────► event
shock ────────────► event
orientation ──────► event
11. Power architecture

This should be designed from day one.

I would make the firmware states:

BOOT
 │
 ▼
INIT
 │
 ▼
ZIGBEE_START
 │
 ▼
RUNNING
 │
 ├──── sensor timer ────► measurement
 │
 ├──── LIS2DH interrupt ► motion
 │
 ├──── BLE request ─────► service mode
 │
 └──── no activity ─────► SLEEP

The critical question is when BLE is enabled.

I would not advertise BLE continuously.

Instead:

Normal:
    BLE OFF

User presses button / special motion pattern:
    BLE advertising ON

After 2 minutes:
    BLE OFF

If your PCB doesn't have a physical button, the LIS2DH12 can potentially provide a "shake to wake/service" mechanism.

12. Dual-protocol behavior

Conceptually:

                 nRF54L15
                     │
          ┌──────────┴──────────┐
          │                     │
        Zigbee                 BLE
          │                     │
       Radio                  Radio
          │                     │
          └──────────┬──────────┘
                     │
              Application
                     │
        ┌────────────┼────────────┐
        │            │            │
      Sensors       Events       Flash

The nRF54L15 is specifically designed as a multiprotocol 2.4 GHz device supporting both Bluetooth LE and 802.15.4.

For the BLE side, current Nordic documentation also describes concurrent/non-concurrent multiprotocol operation.

So I would build around Nordic's current nRF Connect SDK rather than trying to reproduce the older nRF5 SDK timeslot architecture.

13. Development phases

I'd do this in 7 stages, rather than trying to implement everything at once.

Phase 1 — Hardware bring-up
nRF54L15
 │
 ├── SHT40       ✓
 ├── LIS2DH12    ✓
 ├── LPS22DP     ✓
 ├── RGB LED     ✓
 └── SPI NOR     ✓
Phase 2 — BLE

Build:

BLE advertising
      ↓
GATT service
      ↓
read sensors
      ↓
configuration
      ↓
log download
Phase 3 — Flash logger
sensor → flash
event  → flash

Verify power-loss recovery and wear handling.

Phase 4 — Zigbee

Start with:

Zigbee End Device
      ↓
Join network
      ↓
Temperature
Humidity
Pressure
Battery
Phase 5 — Motion

Add:

Motion
Shock
Free fall
Orientation
Tamper
Phase 6 — Low power

Optimize:

sensor duty cycle
Zigbee polling
BLE advertising
LIS2DH12 interrupt
flash writes
Phase 7 — OTA + production

Finally:

Zigbee OTA
BLE service/update
secure boot
factory provisioning
device calibration
production test
14. One thing I'd change from the original concept

I'd make the product identity "Smart Tag", not "Zigbee Temperature Sensor."

That gives you room to evolve it:

             SMART TAG
                 │
      ┌──────────┼──────────┐
      │          │          │
 Environment    Asset     Shipping
    Tag          Tag        Tag
      │          │          │
      └──────────┼──────────┘
                 │
           Same hardware
           Same firmware

And your application mode can determine which features are enabled.

Let's start with the actual project

I'd suggest we build the project around nRF Connect SDK + Zephyr, with this initial target:

nRF54L15 Smart Tag V1
────────────────────────────────

Communication
  ✓ Zigbee Sleepy End Device
  ✓ BLE peripheral

Sensors
  ✓ SHT40
  ✓ LIS2DH12
  ✓ LPS22DP

Storage
  ✓ 2 MB SPI NOR

UI
  ✓ RGB LED

Zigbee
  ✓ Basic
  ✓ Identify
  ✓ Power Configuration
  ✓ Temperature Measurement
  ✓ Humidity Measurement
  ✓ Pressure Measurement
  ✓ OTA
  ✓ Smart Tag custom cluster

BLE
  ✓ Device Information
  ✓ Sensor Service
  ✓ Configuration Service
  ✓ Event Service
  ✓ Log Download Service

Power
  ✓ RTC wakeup
  ✓ Sensor interrupts
  ✓ BLE on-demand
  ✓ Zigbee sleepy operation