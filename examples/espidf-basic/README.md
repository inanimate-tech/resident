# espidf-basic

Minimal ESP-IDF example showing how a consumer project integrates Resident.
Builds a no-op `BasicDevice` that registers a stub LED driver with the
sandbox. Doesn't connect to any real network — `host` is `example.com`.

## Prerequisites

- ESP-IDF v5.5.x installed and sourced (`. $IDF_PATH/export.sh`).
- `git` on PATH (for the dependency fetch step below).

## Build

From this directory:

```bash
./tools/fetch-deps.sh
idf.py build
```

The fetch step clones [Esp32Lua](https://github.com/Fischer-Simon/Esp32Lua)
and shims it as an IDF component under `components/Esp32Lua/`. It's pinned
by commit SHA, idempotent (skips if already present), and the `components/`
directory is gitignored.

## Flash

```bash
idf.py -p /dev/cu.usbserial-XXXX flash monitor
```

Replace `/dev/cu.usbserial-XXXX` with your board's serial device.

## Files

- `main/main.cpp` — `app_main()` entry point and `BasicDevice` class.
- `main/StubLEDDriver.{h,cpp}` — minimal `Resident::Driver` implementation
  that exposes a `led.set(r, g, b)` Lua function (no-op).
- `main/idf_component.yml` — dependency manifest. Resident comes from the
  in-tree source via `path: ../../..`; the rest are registry pins.
- `main/CMakeLists.txt` — IDF component registration.
- `partitions.csv`, `sdkconfig.defaults` — minimal IDF project config.
- `tools/fetch-deps.sh` — fetches Esp32Lua (the only dep that isn't on
  the ESP Component Registry).
