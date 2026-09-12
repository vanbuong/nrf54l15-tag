# Host tests (Unity)

The firmware image is still built with nRF Connect SDK / Zephyr. These
tests do not replace a board; they lock the policy that is expensive to
debug over RTT (DESIGN.md §13).

**No nRF Connect SDK, west, J-Link, or hardware is required.** The suite
compiles the policy units with host `gcc` against tiny Zephyr stubs.

The same three commands run on every GitHub Actions PR
(`.github/workflows/ci.yml`).

---

## 1. Setup

### Packages

Ubuntu / Debian (same set as CI):

```
sudo apt-get update
sudo apt-get install -y gcc make python3 lcov cppcheck
```

| Package | Needed for |
|---|---|
| `gcc`, `make`, `python3` | `make -C tests/unit test` |
| `lcov` | HTML coverage under `coverage/index.html` |
| `cppcheck` | `make -C tests/unit cppcheck` |

`gcov` ships with `gcc`. The Makefile forces **gcc** even if `CC=clang`,
because clang `--coverage` often needs compiler-rt.

Unity v2.6.1 is vendored at `tests/unit/unity/` (MIT). Do not fetch it.

### Check the tools

```
gcc --version
make --version
python3 --version
gcov --version
lcov --version        # optional
cppcheck --version    # optional
```

---

## 2. Run

From the **repository root**:

```
make -C tests/unit test        # compile + run
make -C tests/unit coverage    # gcov + 80 % line gate + HTML if lcov is present
make -C tests/unit cppcheck    # static analysis of the same units
make -C tests/unit clean
```

First run compiles `tests/unit/build/test_smart_tag` and executes it.
Later runs rebuild only what changed.

### Expected pass

The last lines look like:

```
-----------------------
69 Tests 0 Failures 0 Ignored
OK
```

Any failure exits non-zero (the CI gate).

### Coverage

`make -C tests/unit coverage` re-runs the suite, then:

1. `gcov` on the policy sources.
2. `tests/unit/check_coverage.py --min 80` — fails the build if any listed
   `.gcov` is under 80 % line coverage.
3. If `lcov` is installed, HTML at **`coverage/index.html`**.

The 80 % floor applies only to:

`motion_classifier`, `shock_detector`, `motion_manager`,
`zigbee_reporting`, `app_config`, `app_alarms`, `flash_manager`,
`config_migrate`.

Drivers and the BLE/Zigbee stacks are excluded.

### cppcheck

Fails on warning/error (`--error-exitcode=1`). `missingInclude` and
`unusedFunction` are suppressed because the host build has no Zephyr
headers and does not link every firmware function.

---

## 3. What is tested

| Suite | Source | Notes |
|---|---|---|
| motion classifier | `src/motion/motion_classifier.c` | activity, orientation |
| shock detector | `src/motion/shock_detector.c` | latch / re-arm, peak |
| motion manager | `src/motion/motion_manager.c` | start/stop timeout, tamper |
| Zigbee reporting | `src/zigbee/zigbee_reporting.c` | deltas, heartbeat, gas |
| config validation | `src/app/app_config.c` | ranges, mode interval |
| alarm policy | `src/app/app_alarms.c` | latch, hysteresis, gas floor |
| flash logger | `src/storage/flash_manager.c` | CRC, wrap, torn write (RAM mock) |
| config migrate | `src/storage/config_migrate.c` | v3 blobs, schema 1 header |
| wire types | `src/app/smart_tag.h` | packing, battery map, log record, epoch |

Cases are listed in DESIGN.md §13.2 (`UT-*`). The runner is
`tests/unit/test_runner.c`.

---

## 4. How the stubs work

`tests/unit/stubs/zephyr/` is a tiny fake of the Zephyr headers those
units include (`k_mutex`, `LOG_*`, `flash_area_*`, `crc16_ccitt`). It is
not a Zephyr port.

Include order (see `tests/unit/Makefile`): **stubs first**, then `src/`.

Flash tests run against a 256-byte RAM NOR that erases to `0xff` and can
only clear bits on write, which is enough to prove torn-write recovery
and sector wrap.

Do not name a test variable `log` — it collides with `log()` in
`math.h` on some hosts.

---

## 5. Adding a test

1. Put `test_<area>.c` next to the other files under `tests/unit/`.
2. Declare and `RUN_TEST(...)` it in `test_runner.c`.
3. If you compile a new `src/` file, add it to `POLICY_SRCS` and to the
   coverage list in `tests/unit/Makefile`.
4. `make -C tests/unit test coverage`.
