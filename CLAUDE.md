# Resident

Sandboxed Lua runtime for ESP32 devices, with hardware IO and hot reload over
the network. Apps are uploaded as Lua source and run against a small driver
surface (display, buttons, sensors, etc.). Pairs with
[Courier](https://github.com/inanimate-tech/courier) for connectivity.

## Layout

- `src/` — the firmware library. `Resident::Sandbox` is the entry point;
  `ResidentSandboxConfig.h` is the user-facing config struct.
- `examples/` — buildable PlatformIO / ESP-IDF projects, one per board.
  Currently `m5stick-demo`, `adafruit-esp32-s2-feather`, `espidf-basic`,
  and `m5stick-claude-code` (a permission-hook relay).
- `test/unit/` — PlatformIO native unit tests with hand-rolled stubs for
  Arduino / Courier / ezTime headers under `test/unit/include/`.
- `tools/` — `run-tests.py` (test runner), `agent-plugin/` (the Resident
  Claude Code plugin and its skills).
- `docs/` — `start-building.md`, `api.md`, `changelog.md`,
  `migration-0.4-to-0.5.md`.

## Common commands

```bash
# Tests (all)
./tools/run-tests.py all

# Subcommands
./tools/run-tests.py unit                 # PlatformIO native unit tests
./tools/run-tests.py static-analysis      # cppcheck
./tools/run-tests.py build                # build all example PlatformIO envs

# Build one example
cd examples/m5stick-demo/device && pio run
```

The runner self-installs its Python deps via `uv` (PEP 723 inline metadata).

## Conventions

- **Public API** lives under the `Resident::` namespace, declared in headers
  under `src/`. The current API is centred on `Resident::Sandbox` (renamed
  from `Resident::Device` in 0.5 — see `docs/migration-0.4-to-0.5.md`).
- **Examples are independent PlatformIO projects.** Each has its own
  `platformio.ini` and uses `lib_deps = symlink://../../..` to pull in this
  library from the repo root. They should build standalone.
- **Native test stubs** under `test/unit/include/` shadow Arduino-only headers
  so tests can compile without the full ESP32 toolchain. If a Resident header
  pulls in a new Arduino type, the stub set will need a corresponding shim.
- **DEVICE-SKILL.md** in each example describes the Lua module surface for
  that board's drivers, for use by the Resident agent skills.

## Repo hygiene

This is a public open-source repo. A few things to keep out of tracked files:

- Absolute home paths (`/Users/...`, `/home/...`). Use repo-relative paths or
  `<repo-root>` placeholders.
- Per-developer Claude Code artefacts (`.claude/settings.local.json`,
  `.claude/napkin.md`, `.claude/projects/`) — all `.gitignore`-d.
- Working files from plan/spec skills (Superpowers `writing-plans`,
  `writing-skills`, etc.). Save them outside the repo, not under
  `docs/superpowers/`. That path is `.gitignore`-d as a safety net.
- Personal account identifiers in URLs and example configs. For Cloudflare
  Worker hostnames, use a `YOUR-CF-ACCOUNT` placeholder.
