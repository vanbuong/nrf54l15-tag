# Board support

This directory holds two things.

## `holyiot/holyiot_25025/` - the product board

The first-class board definition for the HOLyiot 25025 Beacon V1.0 hardware.
This is what the product firmware is built against:

    west build -b holyiot_25025/nrf54l15/cpuapp -p always .

Zephyr adds the application directory to `BOARD_ROOT` automatically, so the
board is found without any extra arguments. The out-of-tree vendor prefix is
declared in `dts/bindings/vendor-prefixes.txt` at the repository root.

See `holyiot/holyiot_25025/README.md` for the pin map and the known hardware
limitations.

## `nrf54l15dk_nrf54l15_cpuapp.overlay` - DK bring-up reference

An application overlay that applies the same pin map on top of the Nordic
nRF54L15 DK:

    west build -b nrf54l15dk/nrf54l15/cpuapp -p always .

The DK has none of the Smart Tag peripherals fitted, so this build is only
useful for exercising the firmware structure, BLE, and the build itself. It is
kept in sync with the board definition by hand.
