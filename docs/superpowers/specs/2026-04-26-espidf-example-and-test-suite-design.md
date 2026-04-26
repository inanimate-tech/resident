# ESP-IDF example + test suite

> **Status (2026-04-26): IDF example deferred.** Courier 0.3.1 has unresolved transitive deps in its ESP-IDF packaging — `idf.py reconfigure` fails with `Failed to resolve component 'ezTime' required by component 'inanimate__courier': unknown name.` Same applies to `ArduinoJson` and `WiFiManager`. The IDF example portion of this spec is paused pending a courier fix (tracked separately). This PR proceeds with deliverable 2 only — the test suite + CI without the `build-espidf` job. Spec content for the IDF example is kept below as a breadcrumb for resuming once courier is patched.

## Goal

Originally two deliverables, one PR. Currently scoped to deliverable 2:

1. ~~A minimal **ESP-IDF example** (`examples/espidf-basic/`) demonstrating how a real consumer integrates Outrun in an ESP-IDF project — analogous to `courier/examples/espidf-basic/`.~~ **Deferred.**
2. A **test suite + CI** that builds the existing PlatformIO `m5stick-demo`, runs cppcheck on `src/`, and reserves a native unit-test slot for future PRs. (Originally also covered the IDF example build; that job drops out of CI for now.)

The CI must run locally via a single `./tools/run-tests.py` entry point and in GitHub Actions. The unit-test slot starts as one passing assertion; the goal is to give every future "big change" a place to land tests, not to backfill tests for existing code.

## Scope

**In scope:**
- New `examples/espidf-basic/` IDF project that exercises `Outrun::Device`, `Outrun::Sandbox`, and one `OutrunDriver` (a stub LED driver).
- `examples/espidf-basic/tools/fetch-deps.sh` to clone Esp32Lua and shim it as an IDF component.
- `tools/run-tests.py` (uv inline-script) with `static-analysis` / `unit` / `build` / `all` subcommands.
- `test/unit/` PlatformIO native env with one smoke assertion.
- `.github/workflows/ci.yml` with four jobs: static-analysis, unit-tests, build-platformio, build-espidf.
- Updates to `docs/changelog.md` (`v0.3.0-dev` section) recording the new example, test suite, and CI.
- Updates to `.gitignore` to exclude IDF build artifacts (`build/`, `components/`, `managed_components/`, `sdkconfig`, `dependencies.lock`) under the new example.
- Targeted edit to `examples/m5stick-demo/device/platformio.ini` to make the existing demo buildable in CI: replace the `git+ssh://...outrun.git` dep with `symlink://../../..` (uses in-tree source) and the `git+ssh://...courier.git` dep with `git+https://...courier.git` (courier is public). No other changes to the demo. **Why required:** outrun is a private GitHub repo; CI runners have no SSH key for it, so any `git+ssh://` dep on outrun will fail at lib resolution time.

**Not in scope:**
- Backfilling unit tests for existing Outrun source.
- Migrating the M5Stick demo to ESP-IDF.
- Building the M5Stick demo's `server/` (Cloudflare Worker) in CI.
- Publishing Outrun to the ESP component registry (separate future task).
- Documentation rewrites beyond the new example's `README.md` and the changelog entry.

## Repo layout (after this PR)

```
outrun/
├── examples/
│   ├── espidf-basic/                      # NEW
│   │   ├── CMakeLists.txt                 # boilerplate IDF project file
│   │   ├── partitions.csv                 # default + nvs sized for arduino-esp32
│   │   ├── sdkconfig.defaults             # CONFIG_AUTOSTART_ARDUINO=n + flash + tick rate
│   │   ├── README.md                      # build/flash instructions
│   │   ├── main/
│   │   │   ├── CMakeLists.txt             # idf_component_register(... REQUIRES outrun arduino-esp32)
│   │   │   ├── idf_component.yml          # path: ../../.. for outrun; registry pins for the rest
│   │   │   ├── main.cpp                   # app_main() → initArduino() → BasicDevice
│   │   │   ├── StubLEDDriver.h
│   │   │   └── StubLEDDriver.cpp
│   │   ├── components/                    # populated by fetch-deps.sh (gitignored)
│   │   └── tools/
│   │       └── fetch-deps.sh              # clones Esp32Lua, writes shim CMakeLists.txt
│   └── m5stick-demo/                      # unchanged
├── test/
│   └── unit/
│       ├── platformio.ini                 # native env
│       └── test_smoke/
│           └── test_smoke.cpp             # one passing assertion
├── tools/
│   └── run-tests.py                       # uv inline-script, click subcommands
├── .github/
│   └── workflows/
│       └── ci.yml                         # 4 jobs
└── .gitignore                             # add examples/espidf-basic/{build,components,managed_components,sdkconfig,dependencies.lock}
```

## Dependency strategy

The example resolves dependencies the way a real consumer would:

| Dep | Source | Notes |
|---|---|---|
| outrun | `path: ../../..` in `main/idf_component.yml` | builds in-tree code so the example serves as build verification |
| espressif/arduino-esp32 | registry, `^3.0.0` | required by outrun's CMake `REQUIRES` |
| inanimate/courier | registry, `>=0.3.0` | already published to ESP component registry |
| bblanchon/arduinojson | registry, `^7.0.0` | confirmed available as `bblanchon/arduinojson` (lowercase) |
| Esp32Lua | fetched by `tools/fetch-deps.sh` into `components/Esp32Lua/` | not on registry; only library that needs out-of-band fetching |

The fetch script clones the upstream Esp32Lua repository at a pinned tag, removes its `.git`, and writes a shim `CMakeLists.txt` matching the proven pattern from `hawthorn-firmware/arduino-deps/Esp32Lua/`. The exact upstream git URL must be confirmed during implementation by inspecting the PlatformIO registry entry for `fischer-simon/Esp32Lua` — the PlatformIO library identifier doesn't always map directly to a GitHub URL.

Shim CMakeLists.txt:

```cmake
file(GLOB LUA_SRCS "src/lua/*.c")
list(FILTER LUA_SRCS EXCLUDE REGEX "lua\\.c$|luac\\.c$")
idf_component_register(SRCS ${LUA_SRCS} INCLUDE_DIRS "src")
```

The script is idempotent (skips if the target dir exists) and sets `set -euo pipefail`.

## Component details

### `examples/espidf-basic/main/main.cpp`

Demonstrates the meaningful Outrun surface: subclassing `Outrun::Device`, registering a driver, initializing the sandbox. No real hardware:

```cpp
#include <Arduino.h>
#include <OutrunDevice.h>
#include "StubLEDDriver.h"

StubLEDDriver led;

Outrun::DeviceConfig makeConfig() {
    Outrun::DeviceConfig cfg;
    cfg.deviceType = "espidf-basic";
    cfg.host = "example.com";
    return cfg;
}

class BasicDevice : public Outrun::Device {
public:
    BasicDevice() : Outrun::Device(makeConfig()) {}

    void deviceSetup() override {
        sandbox().addDriver(&led);
        sandbox().initialize();
    }

    void deviceLoop() override {}
};

BasicDevice device;

extern "C" void app_main() {
    initArduino();
    device.setup();
    while (true) {
        device.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### `StubLEDDriver`

Implements the `OutrunDriver` interface as a no-op. The implementation should match whatever `OutrunDriver`'s contract actually is (read `src/OutrunDriver.h` during implementation — the header dictates the required overrides). The point is to compile-check `addDriver()` against the published API.

### `examples/espidf-basic/sdkconfig.defaults`

Minimum required settings (more may be added during implementation if the build requires them):

```
CONFIG_AUTOSTART_ARDUINO=n
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_FREERTOS_HZ=1000
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
```

Mirrors the relevant subset of `hawthorn-firmware/time-p1/sdkconfig.defaults`. Adjustments during implementation are expected.

### `tools/run-tests.py`

uv inline-script with `click` subcommands. Mirrors `courier/tools/run-tests.py` structure:

| Subcommand | Action |
|---|---|
| `static-analysis` | `cppcheck --enable=warning --error-exitcode=1 src/` with the same suppression set courier uses (`missingIncludeSystem`, `unmatchedSuppression`, `noCopyConstructor`, `noOperatorEq`) |
| `unit` | `pio test -e native` in `test/unit/` |
| `build` | `pio run` in `examples/m5stick-demo/device/` |
| `all` | invokes the three above in order |

The script auto-prepends `~/.platformio/penv/bin` to `PATH` (matches courier) so `pio` resolves locally without manual setup.

ESP-IDF builds are NOT in `run-tests.py` — they're invoked directly via `idf.py build` locally and via `espressif/esp-idf-ci-action@v1` in CI. This matches courier and avoids forcing IDF setup on contributors who only need to run the PIO checks.

### `test/unit/`

Minimal PlatformIO native env. `platformio.ini`:

```ini
[env:native]
platform = native
test_framework = unity
```

`test_smoke/test_smoke.cpp`:

```cpp
#include <unity.h>

void test_smoke(void) {
    TEST_ASSERT_TRUE(true);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_smoke);
    return UNITY_END();
}
```

The slot exists so future PRs adding real unit tests don't have to invent test infrastructure first.

### `.github/workflows/ci.yml`

Four jobs, all on `ubuntu-latest`, triggered on push to `main` and PRs targeting `main`:

| Job | Setup | Runs |
|---|---|---|
| `static-analysis` | install uv + cppcheck | `./tools/run-tests.py static-analysis` |
| `unit-tests` | install uv + platformio | `./tools/run-tests.py unit` |
| `build-platformio` | install uv + platformio | `./tools/run-tests.py build` |
| `build-espidf` | run `./examples/espidf-basic/tools/fetch-deps.sh` | `espressif/esp-idf-ci-action@v1` with `esp_idf_version: v5.1.4`, `target: esp32`, `path: examples/espidf-basic` |

IDF version `v5.1.4` matches courier for cross-debugging convenience. Single target (`esp32`) to start; adding `esp32s3` later is a one-line change.

## Risks

| # | Risk | Mitigation |
|---|---|---|
| 1 | Pin drift in fetch script (Esp32Lua tag force-moved or repo deleted) | Accepted — same exposure courier has, no incidents to date. Consider mirror or vendoring if it becomes a problem. |
| 2 | Network flake in CI fetching Esp32Lua | Accepted. If it becomes a problem, cache via `actions/cache`. |
| 3 | Dead-weight unit-test slot if no tests get added | Accepted. Deletion is one commit. The cost of having it is near-zero; the cost of recreating it later is non-zero. |
| 4 | Courier dep version drift between `library.json` and the example's pinned registry version | Accepted. Bumping the registry pin in `idf_component.yml` is intentional and obvious in diffs. |
| 5 | `espressif/esp-idf-ci-action@v1` runtime overhead (image pull) | Accepted. Courier uses it without issue; CI time is acceptable for one IDF target. |

Risks previously raised and resolved by the time-p1 reference:
- ~~Esp32Lua won't compile under IDF~~ — works with the 2-line shim.
- ~~outrun won't compile under IDF~~ — already builds under IDF in time-p1.

## Success criteria

- `./tools/run-tests.py all` passes locally.
- `idf.py build` in `examples/espidf-basic/` succeeds locally after running `tools/fetch-deps.sh`.
- All four CI jobs pass on the PR.
- The new example's `README.md` documents the build/flash workflow clearly enough that a fresh checkout works.
- `docs/changelog.md` `v0.3.0-dev` section records the addition under "New features" with a brief note about the test suite under "Internal".

## Out of scope reminders

- No registry publication of outrun in this PR.
- No changes to existing `src/` beyond minimal fixes required for the example to compile.
- No removal of the existing PlatformIO m5stick-demo or its build path.
