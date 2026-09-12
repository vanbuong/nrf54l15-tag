# Host tests (Unity)

The firmware image is still built with nRF Connect SDK / Zephyr. These
tests do not replace a board; they lock the policy that is expensive to
debug over RTT (DESIGN.md §13).

## What is tested

| Suite | Source | Notes |
|---|---|---|
| motion classifier | `src/motion/motion_classifier.c` | activity, orientation |
| shock detector | `src/motion/shock_detector.c` | latch / re-arm, peak |
| Zigbee reporting | `src/zigbee/zigbee_reporting.c` | deltas, heartbeat, gas |
| config validation | `src/app/app_config.c` | ranges, mode interval |
| flash logger | `src/storage/flash_manager.c` | CRC, wrap, torn write (RAM mock) |
| wire types | `src/app/smart_tag.h` | packing, battery map, log record |

Framework: [Unity](https://github.com/ThrowTheSwitch/Unity) v2.6.1, vendored
under `tests/unit/unity/` (MIT).

## Commands

From the repository root:

```
make -C tests/unit test        # run the suite
make -C tests/unit coverage    # gcov + 80 % gate + HTML if lcov is installed
make -C tests/unit cppcheck    # static analysis of the same units
make -C tests/unit clean
```

Coverage HTML lands in `coverage/index.html`. The 80 % line-coverage
floor applies to the five policy sources above, not to Zephyr drivers or
the BLE/Zigbee stacks.

## How the stubs work

`tests/unit/stubs/zephyr/` is a tiny fake of the Zephyr headers those
units include (`k_mutex`, `LOG_*`, `flash_area_*`, `crc16_ccitt`). It is
not a Zephyr port. Flash tests run against a 256-byte RAM NOR that
erases to `0xff` and can only clear bits on write, which is enough to
prove torn-write recovery and sector wrap.
