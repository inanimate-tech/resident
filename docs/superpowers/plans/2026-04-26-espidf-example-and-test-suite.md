# ESP-IDF example + test suite — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an ESP-IDF example demonstrating Outrun integration in a real-consumer style, plus a CI-driven test suite that builds the new IDF example, the existing PlatformIO `m5stick-demo`, runs cppcheck on `src/`, and reserves a native unit-test slot for future PRs.

**Architecture:** A single `tools/run-tests.py` (uv inline-script) drives the PIO/cppcheck checks locally and in CI. The ESP-IDF build is invoked via `idf.py` locally and via `espressif/esp-idf-ci-action@v1` in CI (matches courier's split). The new IDF example uses `path: ../../..` for outrun, registry pins for arduino-esp32/courier/ArduinoJson, and a single fetch script for Esp32Lua (the only dep with no registry option). The existing m5stick-demo gets a small platformio.ini change to use the in-tree outrun source via `symlink://` (required because outrun is a private GitHub repo and CI has no SSH key).

**Tech Stack:** ESP-IDF v5.1.4, PlatformIO, espressif/arduino-esp32 ^3.0.0, courier ≥0.3.0 (registry), ArduinoJson ^7 (registry), Esp32Lua (fetched), cppcheck, Unity (PIO native test framework), GitHub Actions, uv inline-script + click for `run-tests.py`.

---

## File Structure

**Create:**
- `examples/espidf-basic/CMakeLists.txt` — IDF top-level project file
- `examples/espidf-basic/partitions.csv` — minimal 4MB partition table
- `examples/espidf-basic/sdkconfig.defaults` — IDF config defaults
- `examples/espidf-basic/README.md` — build/flash instructions
- `examples/espidf-basic/main/CMakeLists.txt` — component registration
- `examples/espidf-basic/main/idf_component.yml` — dependencies
- `examples/espidf-basic/main/main.cpp` — `app_main` entry point
- `examples/espidf-basic/main/StubLEDDriver.h` — driver header
- `examples/espidf-basic/main/StubLEDDriver.cpp` — driver impl
- `examples/espidf-basic/tools/fetch-deps.sh` — clones and shims Esp32Lua
- `test/unit/platformio.ini` — native PIO env
- `test/unit/test_smoke/test_smoke.cpp` — one passing assertion
- `tools/run-tests.py` — uv inline-script with click subcommands
- `.github/workflows/ci.yml` — four CI jobs

**Modify:**
- `.gitignore` — add IDF artifact patterns
- `examples/m5stick-demo/device/platformio.ini` — swap `git+ssh://` outrun dep to `symlink://../../..`, swap `git+ssh://` courier dep to `git+https://`
- `docs/changelog.md` — add entries to `v0.3.0-dev` section

---

## Preconditions (one-time)

The implementer must have these tools installed locally to verify the plan as they execute:
- ESP-IDF v5.1.4 sourced (`. $IDF_PATH/export.sh` or `get_idf` alias)
- PlatformIO (`pio` on PATH or installed under `~/.platformio/penv/bin`)
- `cppcheck` (`brew install cppcheck` on macOS)
- `uv` (used to run the inline-script `run-tests.py`)

If any tool is missing, install it before proceeding rather than skipping the verification steps.

---

## Task 1: Add `.gitignore` rules for IDF artifacts

**Files:**
- Modify: `.gitignore`

- [ ] **Step 1: Append IDF and PIO artifact patterns**

The current `.gitignore` covers `.pio/`, `build/`, `.vscode/`, `*.o`, `*.a`. Append the IDF-specific paths and unit-test build paths so they don't get committed.

Add to the end of `/Users/matt/code/outrun/.gitignore`:

```
# ESP-IDF build artifacts (per-example)
examples/*/build/
examples/*/managed_components/
examples/*/components/
examples/*/sdkconfig
examples/*/sdkconfig.old
examples/*/dependencies.lock

# PlatformIO test build artifacts
test/unit/.pio/
```

Note: the existing `build/` and `.pio/` rules already match nested paths, so the additions above are belt-and-braces explicit. The key addition is `examples/*/components/` (fetched Esp32Lua source goes here and must NOT be committed) and `examples/*/sdkconfig` (per-board generated config).

- [ ] **Step 2: Verify .gitignore parses correctly**

Run: `git check-ignore -v examples/espidf-basic/components/Esp32Lua/foo.c`
Expected: outputs the matching rule (the path doesn't have to exist).

Run: `git check-ignore -v examples/espidf-basic/sdkconfig`
Expected: outputs the matching rule.

- [ ] **Step 3: Commit**

```bash
git add .gitignore
git commit -m "chore: ignore ESP-IDF and unit-test build artifacts"
```

---

## Task 2: Confirm Esp32Lua upstream repo URL and pinned tag

**Files:** none modified — this is a recon task whose output is a documented decision used in Task 6.

- [ ] **Step 1: Check the PlatformIO library registry entry**

Run: `pio pkg show fischer-simon/Esp32Lua | head -30`
Expected: shows the library metadata including a `repository` URL.

If `pio pkg show` is unavailable, fall back to: `curl -s https://api.registry.platformio.org/v3/libraries/fischer-simon/library/Esp32Lua | python3 -m json.tool | grep -E '(repository|version)' | head -10`

- [ ] **Step 2: Verify the repository is reachable and pick a tag**

Take the URL from Step 1. Verify it's reachable: `git ls-remote --tags <url> | tail -10`
Expected: lists git tags. Pick a stable tag matching the PlatformIO version pin in `library.json` (`^5.4.7`) — e.g. `v5.4.7` or whatever the closest tag is.

If the upstream repo doesn't have a matching tag (some Arduino libs only tag major versions), pick the highest available tag in the same major (5.x) and document the choice.

- [ ] **Step 3: Record the decision**

Write the chosen URL and tag in a comment at the top of the fetch script you'll create in Task 6. No commit yet — this is just a captured decision used downstream.

Example (to use in Task 6):
```bash
# Esp32Lua upstream: https://github.com/<actual-url>/Esp32Lua
# Tag: v5.4.7 (matches library.json pin "^5.4.7")
```

---

## Task 3: Scaffold the IDF example project (no app code yet)

**Files:**
- Create: `examples/espidf-basic/CMakeLists.txt`
- Create: `examples/espidf-basic/partitions.csv`
- Create: `examples/espidf-basic/sdkconfig.defaults`
- Create: `examples/espidf-basic/main/CMakeLists.txt`
- Create: `examples/espidf-basic/main/idf_component.yml`

- [ ] **Step 1: Create the project-level CMakeLists.txt**

Path: `examples/espidf-basic/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espidf-basic)
```

- [ ] **Step 2: Create the partition table**

Path: `examples/espidf-basic/partitions.csv`

```
# Name,   Type, SubType, Offset,   Size, Flags
nvs,      data, nvs,     0x9000,   0x6000,
phy_init, data, phy,     0xf000,   0x1000,
factory,  app,  factory, 0x10000,  3M,
```

This is a minimal 4MB layout. arduino-esp32 needs the `factory` slot for the app and `nvs` for WiFi creds.

- [ ] **Step 3: Create sdkconfig.defaults**

Path: `examples/espidf-basic/sdkconfig.defaults`

```
# Target — esp32 by default; override with idf.py set-target esp32s3 etc.
CONFIG_IDF_TARGET="esp32"

# Flash (4MB default; matches partitions.csv above)
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y

# Arduino integration: don't autostart; we drive setup()/loop() via app_main()
CONFIG_AUTOSTART_ARDUINO=n

# Arduino requires 1000Hz tick rate
CONFIG_FREERTOS_HZ=1000

# Main task stack — registration + JSON parsing needs more than the 3584 default
CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384

# Use our custom partition table
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"
```

- [ ] **Step 4: Create the main component CMakeLists.txt**

Path: `examples/espidf-basic/main/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "main.cpp" "StubLEDDriver.cpp"
    INCLUDE_DIRS "."
    REQUIRES outrun espressif__arduino-esp32
)
```

The `REQUIRES outrun` line works because Task 4's `idf_component.yml` will declare outrun via `path:`, which makes it a transitive component named `outrun`.

- [ ] **Step 5: Create the main component idf_component.yml**

Path: `examples/espidf-basic/main/idf_component.yml`

```yaml
dependencies:
  idf:
    version: ">=5.0.0"

  # Outrun itself — built from the in-tree source so the example is also
  # build verification for the library. Consumers copying this example
  # should change this to a pinned registry version once outrun publishes.
  outrun:
    path: ../../..

  # Registry deps — these are what real consumers depend on.
  espressif/arduino-esp32:
    version: "^3.0.0"
  inanimate/courier:
    version: ">=0.3.0"
  bblanchon/arduinojson:
    version: "^7.0.0"

  # Esp32Lua is not on the ESP component registry. It's fetched and shimmed
  # into examples/espidf-basic/components/Esp32Lua/ by tools/fetch-deps.sh.
  # Run that script BEFORE `idf.py build`.
```

- [ ] **Step 6: Verify the project at least configures**

The project won't build yet (no main.cpp, no fetched Esp32Lua), but `idf.py reconfigure` should resolve the registry deps and fail at the missing source/component step, not at YAML parsing.

Run from `examples/espidf-basic/`:
```bash
idf.py reconfigure 2>&1 | tail -20
```
Expected: registry components download (arduino-esp32, courier, ArduinoJson). Failure messages should be about missing `Esp32Lua` component or `main.cpp`, NOT YAML/CMake parse errors.

If you see YAML parse errors, fix `idf_component.yml` formatting.
If you see "REQUIRES outrun" can't be found, your `path: ../../..` doesn't resolve — verify the path is correct (`outrun/CMakeLists.txt` exists 3 levels up from `main/`).

- [ ] **Step 7: Commit**

```bash
git add examples/espidf-basic/
git commit -m "feat(examples): scaffold espidf-basic IDF project"
```

---

## Task 4: Implement `StubLEDDriver`

**Files:**
- Create: `examples/espidf-basic/main/StubLEDDriver.h`
- Create: `examples/espidf-basic/main/StubLEDDriver.cpp`

The Outrun driver interface (`src/OutrunDriver.h`) is:

```cpp
namespace Outrun {
class Driver {
public:
  virtual const char* name() const = 0;
  virtual void installSandboxModule(lua_State* L) = 0;
  virtual void onAppReset() {}
  virtual void onAppRunning(bool running) {}
  virtual ~Driver() = default;
protected:
  void sendEvent(const char* name, const EventField* fields, int fieldCount);
};
}
```

So `StubLEDDriver` must implement `name()` and `installSandboxModule()`. The other two have default no-op impls.

- [ ] **Step 1: Write the header**

Path: `examples/espidf-basic/main/StubLEDDriver.h`

```cpp
#pragma once

#include <OutrunDriver.h>

// Minimal no-op driver. Exists to exercise Sandbox::addDriver() in CI without
// requiring real hardware. Exposes an `led` table in Lua with one function:
//   led.set(r, g, b)  -- accepts but ignores three integers
class StubLEDDriver : public Outrun::Driver {
public:
    const char* name() const override { return "led"; }
    void installSandboxModule(lua_State* L) override;
};
```

- [ ] **Step 2: Write the implementation**

Path: `examples/espidf-basic/main/StubLEDDriver.cpp`

```cpp
#include "StubLEDDriver.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace {
int l_set(lua_State* L) {
    // Accept 3 ints, ignore them. Real hardware would drive an LED here.
    luaL_checkinteger(L, 1);
    luaL_checkinteger(L, 2);
    luaL_checkinteger(L, 3);
    return 0;
}

const luaL_Reg led_funcs[] = {
    {"set", l_set},
    {nullptr, nullptr},
};
} // namespace

void StubLEDDriver::installSandboxModule(lua_State* L) {
    luaL_newlib(L, led_funcs);
    lua_setglobal(L, "led");
}
```

- [ ] **Step 3: No standalone test — Task 7 verifies via the full IDF build**

`StubLEDDriver` is exercised when the example links. There's no native test harness for ESP-IDF code in this PR (that's part of the larger Outrun unit test goal, which is out of scope here). The compile + link in Task 7 is the verification.

- [ ] **Step 4: Commit**

```bash
git add examples/espidf-basic/main/StubLEDDriver.h examples/espidf-basic/main/StubLEDDriver.cpp
git commit -m "feat(examples): add StubLEDDriver for IDF example"
```

---

## Task 5: Implement `main.cpp`

**Files:**
- Create: `examples/espidf-basic/main/main.cpp`

- [ ] **Step 1: Write main.cpp**

Path: `examples/espidf-basic/main/main.cpp`

```cpp
// Minimal ESP-IDF example for Outrun. Demonstrates:
//   - Subclassing Outrun::Device
//   - Registering a driver with the sandbox
//   - Running setup()/loop() from app_main() rather than autostarted Arduino
//
// This intentionally targets `example.com` — it won't actually connect.
// Real consumers point `host` at their own Outrun server.

#include <Arduino.h>
#include <OutrunDevice.h>
#include "StubLEDDriver.h"

static StubLEDDriver led;

static Outrun::DeviceConfig makeConfig() {
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

static BasicDevice device;

extern "C" void app_main() {
    initArduino();
    device.setup();
    while (true) {
        device.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

- [ ] **Step 2: Verify the OutrunDeviceConfig fields are right**

The `cfg.deviceType` and `cfg.host` field names come from `src/OutrunDeviceConfig.h`. Open that file and confirm both fields exist with those names. If they differ, adjust `main.cpp` to match.

Run: `grep -E '(deviceType|host)' /Users/matt/code/outrun/src/OutrunDeviceConfig.h`
Expected: both field names appear in the config struct.

If a field name differs, update `main.cpp` accordingly (keep the same intent: identify the device type, set the WS host).

- [ ] **Step 3: Commit**

```bash
git add examples/espidf-basic/main/main.cpp
git commit -m "feat(examples): add main.cpp for IDF example"
```

---

## Task 6: Implement `tools/fetch-deps.sh` and verify the full IDF build

**Files:**
- Create: `examples/espidf-basic/tools/fetch-deps.sh`

- [ ] **Step 1: Write the fetch script using the URL/tag from Task 2**

Path: `examples/espidf-basic/tools/fetch-deps.sh`

Substitute `<ESP32LUA_URL>` and `<ESP32LUA_TAG>` with the values you confirmed in Task 2.

```bash
#!/usr/bin/env bash
# Fetches Esp32Lua and wraps it as an ESP-IDF component under
# examples/espidf-basic/components/. Esp32Lua is not on the ESP Component
# Registry, so a pure `idf.py build` can't resolve it.
#
# Re-runnable: skips clone if the target dir exists.

set -euo pipefail

DIR="$(cd "$(dirname "$0")/.." && pwd)"
COMPONENTS="$DIR/components"
mkdir -p "$COMPONENTS"

ESP32LUA_URL="<ESP32LUA_URL>"
ESP32LUA_TAG="<ESP32LUA_TAG>"

fetch() {
    local name="$1" url="$2" ref="$3"
    local target="$COMPONENTS/$name"
    if [[ -d "$target" ]]; then
        echo "  $name already present — skipping"
        return
    fi
    echo "  fetching $name @ $ref"
    git clone --depth=1 --branch="$ref" --quiet "$url" "$target"
    rm -rf "$target/.git"
}

echo "Fetching Esp32Lua into $COMPONENTS"
fetch Esp32Lua "$ESP32LUA_URL" "$ESP32LUA_TAG"

# Shim CMakeLists for Esp32Lua — upstream ships an Arduino-style library,
# not an IDF component. Glob the Lua C sources, exclude the standalone
# interpreter and compiler entry points, register includes from src/.
cat > "$COMPONENTS/Esp32Lua/CMakeLists.txt" <<'EOF'
file(GLOB LUA_SRCS "src/lua/*.c")
list(FILTER LUA_SRCS EXCLUDE REGEX "lua\\.c$|luac\\.c$")
idf_component_register(SRCS ${LUA_SRCS} INCLUDE_DIRS "src")
EOF

echo "Done."
```

- [ ] **Step 2: Make the script executable**

```bash
chmod +x examples/espidf-basic/tools/fetch-deps.sh
```

- [ ] **Step 3: Run the fetch script and verify it succeeds**

Run from repo root:
```bash
./examples/espidf-basic/tools/fetch-deps.sh
```
Expected: prints "fetching Esp32Lua @ <tag>" and "Done." Creates `examples/espidf-basic/components/Esp32Lua/` containing `src/lua/*.c` and a generated `CMakeLists.txt`.

Verify: `ls examples/espidf-basic/components/Esp32Lua/`
Expected: includes `src/` and `CMakeLists.txt`.

- [ ] **Step 4: Re-run the script to verify idempotency**

```bash
./examples/espidf-basic/tools/fetch-deps.sh
```
Expected: "Esp32Lua already present — skipping". Exit 0.

- [ ] **Step 5: Run the full IDF build**

Make sure ESP-IDF is sourced in your shell. Then from `examples/espidf-basic/`:
```bash
idf.py build
```
Expected: build succeeds. Output ends with "Project build complete." and an `app-flash` instruction.

If the build fails, common fixes:
- Missing field on `Outrun::DeviceConfig` — adjust `main.cpp` to match the real header.
- Missing function from `Outrun::Device` — check `src/OutrunDevice.h` for the actual signatures.
- Linker errors about undefined `lua_*` symbols — Esp32Lua source filter in the shim CMakeLists.txt is too aggressive; check the `src/lua/` directory layout.
- `Arduino.h` not found — `arduino-esp32` registry resolution didn't pull through; check `idf_component.yml` syntax.

Iterate until the build is green. Each fix can be its own micro-commit if it touches multiple files.

- [ ] **Step 6: Commit**

```bash
git add examples/espidf-basic/tools/fetch-deps.sh
# Plus any minor source fixes from Step 5.
git commit -m "feat(examples): add fetch-deps.sh and verify IDF build"
```

---

## Task 7: Add native unit-test slot

**Files:**
- Create: `test/unit/platformio.ini`
- Create: `test/unit/test_smoke/test_smoke.cpp`

- [ ] **Step 1: Write the PlatformIO native env config**

Path: `test/unit/platformio.ini`

```ini
[env:native]
platform = native
test_framework = unity
build_flags =
    -std=gnu++17
    -Wall
```

- [ ] **Step 2: Write the smoke test**

Path: `test/unit/test_smoke/test_smoke.cpp`

```cpp
// Smoke test — proves the native test environment compiles and runs.
// Real unit tests for outrun source land in sibling test_*/ directories.

#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_smoke(void) {
    TEST_ASSERT_TRUE(true);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_smoke);
    return UNITY_END();
}
```

- [ ] **Step 3: Verify the test runs**

Run from repo root:
```bash
cd test/unit && pio test -e native && cd ../..
```
Expected: output ends with `1 test, 0 failures` and exit 0.

If `pio` is missing, install: `pip install platformio` (or use `~/.platformio/penv/bin/pio`).

If the test fails to build, `platform = native` may need additional packaging — the PIO native platform is bundled, so this should work out of the box. If it doesn't, check `pio platform install native`.

- [ ] **Step 4: Commit**

```bash
git add test/unit/
git commit -m "test: add native unit-test slot with smoke test"
```

---

## Task 8: Patch m5stick-demo platformio.ini for CI compatibility

**Files:**
- Modify: `examples/m5stick-demo/device/platformio.ini`

The current file uses `git+ssh://git@github.com/inanimate-tech/outrun.git` (private repo, no SSH key in CI) and `git+ssh://...courier.git` (public, but still SSH). Switch outrun to the in-tree source via `symlink://` and courier to `git+https://` (auth-free).

- [ ] **Step 1: Edit the lib_deps**

In `examples/m5stick-demo/device/platformio.ini`, in the `[env]` block, replace these two lines:

```
    git+ssh://git@github.com/inanimate-tech/outrun.git
    git+ssh://git@github.com/inanimate-tech/courier.git
```

With:

```
    symlink://../../..
    git+https://github.com/inanimate-tech/courier.git
```

The `symlink://../../..` path is relative to the platformio.ini location — it resolves to the repo root, which is outrun.

- [ ] **Step 2: Verify the demo still builds**

Run from `examples/m5stick-demo/device/`:
```bash
pio run -e m5stick 2>&1 | tail -20
```
Expected: "SUCCESS" at the end. The build picks up outrun source from the symlinked path and courier from a fresh https clone.

If the build fails because PIO doesn't recognize `symlink://`, fall back to `lib_extra_dirs = ../../..` in the `[env]` block and remove the outrun line from `lib_deps` entirely (PIO will discover outrun by scanning `lib_extra_dirs` for libraries with manifests). The `library.json` in the repo root is the manifest PIO needs.

- [ ] **Step 3: Commit**

```bash
git add examples/m5stick-demo/device/platformio.ini
git commit -m "fix(examples): m5stick-demo uses in-tree outrun + https courier for CI"
```

---

## Task 9: Implement `tools/run-tests.py`

**Files:**
- Create: `tools/run-tests.py`

- [ ] **Step 1: Write the script**

Path: `tools/run-tests.py`

```python
#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "click>=8.1",
# ]
# ///

import os
import subprocess
import sys
from pathlib import Path

import click

ROOT = Path(__file__).parent.parent

# Ensure ~/.platformio/penv/bin is on PATH (pio is often installed there)
_pio_bin = Path.home() / ".platformio" / "penv" / "bin"
if _pio_bin.is_dir():
    os.environ["PATH"] = str(_pio_bin) + os.pathsep + os.environ.get("PATH", "")

CPPCHECK_SUPPRESSIONS = [
    "missingIncludeSystem",
    "unmatchedSuppression",
    "noCopyConstructor",
    "noOperatorEq",
]


def run_cmd(cmd: list[str], cwd: Path | None = None, label: str = "") -> bool:
    if label:
        click.echo(click.style(f"  → {label}", fg="cyan"))
    return subprocess.run(cmd, cwd=cwd).returncode == 0


@click.group(context_settings={"help_option_names": ["-h", "--help"]})
def cli() -> None:
    """Run tests for the Outrun library."""
    pass


@cli.command("static-analysis")
def static_analysis() -> None:
    """Run cppcheck on src/."""
    click.echo(click.style("Static Analysis", fg="white", bold=True))
    cmd = ["cppcheck", "--enable=warning", "--error-exitcode=1"]
    for s in CPPCHECK_SUPPRESSIONS:
        cmd.append(f"--suppress={s}")
    cmd.append(str(ROOT / "src"))
    if run_cmd(cmd, cwd=ROOT, label="cppcheck src/"):
        click.echo(click.style("✓ Static analysis passed", fg="green"))
    else:
        click.echo(click.style("✗ Static analysis failed", fg="red"))
        sys.exit(1)


@cli.command("unit")
def unit_tests() -> None:
    """Run unit tests on native platform."""
    click.echo(click.style("Unit Tests", fg="white", bold=True))
    test_dir = ROOT / "test" / "unit"
    if not run_cmd(["pio", "test", "-e", "native"], cwd=test_dir, label="pio test -e native"):
        click.echo(click.style("✗ Unit tests failed", fg="red"))
        sys.exit(1)
    click.echo(click.style("✓ Unit tests passed", fg="green"))


@cli.command("build")
def build_verification() -> None:
    """Build-verify the m5stick-demo PlatformIO example."""
    click.echo(click.style("Build Verification", fg="white", bold=True))
    project = ROOT / "examples" / "m5stick-demo" / "device"
    if not run_cmd(["pio", "run"], cwd=project, label=f"pio run ({project.name})"):
        click.echo(click.style("✗ Build failed", fg="red"))
        sys.exit(1)
    click.echo(click.style("✓ Build passed", fg="green"))


@cli.command("all")
@click.pass_context
def run_all(ctx: click.Context) -> None:
    """Run static analysis, unit tests, and build verification."""
    ctx.invoke(static_analysis)
    click.echo()
    ctx.invoke(unit_tests)
    click.echo()
    ctx.invoke(build_verification)


if __name__ == "__main__":
    cli()
```

- [ ] **Step 2: Make it executable**

```bash
chmod +x tools/run-tests.py
```

- [ ] **Step 3: Verify each subcommand works**

Run each separately so a failure points to the right subcommand:

```bash
./tools/run-tests.py static-analysis
```
Expected: "✓ Static analysis passed" and exit 0. (Cppcheck warnings on outrun source may surface here. If real warnings appear, fix them in `src/` — but ONLY if they're real bugs. For style-only complaints, add suppressions to `CPPCHECK_SUPPRESSIONS`.)

```bash
./tools/run-tests.py unit
```
Expected: "✓ Unit tests passed".

```bash
./tools/run-tests.py build
```
Expected: "✓ Build passed". This builds m5stick-demo and takes a couple of minutes.

```bash
./tools/run-tests.py all
```
Expected: all three pass in sequence.

- [ ] **Step 4: Commit**

```bash
git add tools/run-tests.py
# Plus any cppcheck-driven fixes to src/ if applicable.
git commit -m "build: add tools/run-tests.py with static-analysis/unit/build/all"
```

---

## Task 10: Add CI workflow

**Files:**
- Create: `.github/workflows/ci.yml`

- [ ] **Step 1: Write the workflow**

Path: `.github/workflows/ci.yml`

```yaml
name: CI

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  static-analysis:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.11"
      - name: Install uv
        run: pip install uv
      - name: Install cppcheck
        run: sudo apt-get update && sudo apt-get install -y cppcheck
      - name: Run static analysis
        run: ./tools/run-tests.py static-analysis

  unit-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.11"
      - name: Install uv and PlatformIO
        run: pip install uv platformio
      - name: Run unit tests
        run: ./tools/run-tests.py unit

  build-platformio:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.11"
      - name: Install uv and PlatformIO
        run: pip install uv platformio
      - name: Build PlatformIO m5stick-demo
        run: ./tools/run-tests.py build

  build-espidf:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Fetch Esp32Lua
        run: ./examples/espidf-basic/tools/fetch-deps.sh
      - name: Build espidf-basic example
        uses: espressif/esp-idf-ci-action@v1
        with:
          esp_idf_version: v5.1.4
          target: esp32
          path: examples/espidf-basic
```

- [ ] **Step 2: Lint the YAML locally (optional)**

If `actionlint` is installed: `actionlint .github/workflows/ci.yml`
Otherwise skip — GitHub will surface syntax issues on push.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: add four-job workflow (static-analysis, unit, build-pio, build-idf)"
```

---

## Task 11: Write the example README and update the changelog

**Files:**
- Create: `examples/espidf-basic/README.md`
- Modify: `docs/changelog.md`

- [ ] **Step 1: Write the example README**

Path: `examples/espidf-basic/README.md`

```markdown
# espidf-basic

Minimal ESP-IDF example showing how a consumer project integrates Outrun.
Builds a no-op `BasicDevice` that registers a stub LED driver with the
sandbox. Doesn't connect to any real network — `host` is `example.com`.

## Prerequisites

- ESP-IDF v5.1.4 installed and sourced (`. $IDF_PATH/export.sh` or use
  `get_idf` if you've set up the alias).
- `git` on PATH (for the dependency fetch step below).

## Build

From this directory:

```bash
./tools/fetch-deps.sh
idf.py build
```

The fetch step clones Esp32Lua and shims it as an IDF component under
`components/Esp32Lua/`. It's idempotent (skips if already present) and
the components directory is gitignored.

## Flash

```bash
idf.py -p /dev/cu.usbserial-XXXX flash monitor
```

Replace `/dev/cu.usbserial-XXXX` with your board's serial device.

## Files

- `main/main.cpp` — `app_main()` entry point and `BasicDevice` class.
- `main/StubLEDDriver.{h,cpp}` — minimal `Outrun::Driver` implementation
  that exposes a `led.set(r, g, b)` Lua function (no-op).
- `main/idf_component.yml` — dependency manifest. Outrun comes from the
  in-tree source via `path: ../../..`; the rest are registry pins.
- `main/CMakeLists.txt` — IDF component registration.
- `partitions.csv`, `sdkconfig.defaults` — minimal IDF project config.
- `tools/fetch-deps.sh` — fetches Esp32Lua (the only dep that isn't on
  the ESP Component Registry).
```

- [ ] **Step 2: Update the changelog**

In `docs/changelog.md`, modify the `## v0.3.0-dev (d27cda1)` section to add:

```markdown
## v0.3.0-dev (d27cda1)

### New features

- `examples/espidf-basic/`: minimal ESP-IDF example demonstrating Outrun
  integration in a real-consumer style. Uses registry pins for
  arduino-esp32, courier, and ArduinoJson; a small fetch script handles
  Esp32Lua (the only dep not on the ESP Component Registry).

### Internal

- Added `tools/run-tests.py` (uv inline-script) with `static-analysis`,
  `unit`, `build`, and `all` subcommands. Local entry point and CI driver.
- Added `test/unit/` with a native PlatformIO env and a smoke assertion.
  Slot for future unit tests; no existing source is currently covered.
- Added `.github/workflows/ci.yml` with four jobs: static analysis
  (cppcheck), unit tests, PlatformIO build of `m5stick-demo`, and ESP-IDF
  build of `espidf-basic`.
- Patched `examples/m5stick-demo/device/platformio.ini` to use the in-tree
  Outrun source (`symlink://../../..`) and HTTPS for courier, so the demo
  builds in CI without SSH credentials. No functional change for local devs.
```

- [ ] **Step 3: Commit**

```bash
git add examples/espidf-basic/README.md docs/changelog.md
git commit -m "docs: README for espidf-basic + v0.3.0-dev changelog entries"
```

---

## Task 12: Push and verify CI

**Files:** none modified.

- [ ] **Step 1: Push the branch**

```bash
git push
```

- [ ] **Step 2: Watch the CI run**

```bash
gh pr checks 3 --watch
```

(PR #3 is the open changelog-and-v0.3.0-dev branch from earlier work.)

Expected: all four jobs (static-analysis, unit-tests, build-platformio, build-espidf) pass.

- [ ] **Step 3: If a CI job fails, debug and re-push**

For each failure:

```bash
gh run view --log-failed
```

Common failures and fixes:

- **`static-analysis`** fails on a real warning in `src/`: fix the warning and re-push. Don't suppress unless it's a known-false-positive — use the `CPPCHECK_SUPPRESSIONS` list in `tools/run-tests.py` for those.
- **`unit-tests`** fails: PIO native env doesn't compile on Ubuntu. Check the error; usually an `#include` order issue.
- **`build-platformio`** fails: usually an inability to resolve a `lib_deps` entry. Verify the symlink/HTTPS swap in Task 8.
- **`build-espidf`** fails: usually a dep resolution issue. Check the `idf.py build` output for which component is missing. Re-run the fetch script locally to confirm Esp32Lua source is present.

- [ ] **Step 4: Confirm all green and report back to the user**

Once all four jobs are green on the PR, the work is complete. Report the final commit SHA and the PR URL.

---

## Done

The PR now contains:
- Two new commits at the top of the branch (changelog work) plus eleven implementation commits.
- A working ESP-IDF example.
- A test suite that builds locally and in CI.
- A unit-test slot ready for future tests.
- An updated changelog reflecting all of the above.

The next "big change" can land tests in `test/unit/test_*` and rely on the existing CI infrastructure to enforce them.
