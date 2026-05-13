# Resident agent plugin

Purpose: Skills to create and push apps to devices with a Resident sandbox.

A Claude Code plugin for the [Resident](https://github.com/inanimate-tech/resident) ESP32 sandbox platform. Lets a coding agent author Lua apps against a specific device's surface, validate them locally, and push them to a running device over the relay — all without leaving the chat.

## Quick start

Once you've brought up your hardware (see [`docs/start-building.md`](../../docs/start-building.md)) and the device is showing its 8-character device ID, try:

```
/resident:push-app display Hello, World! on my new device
```

The skill chain generates a small Lua app for your device's surface, validates it locally, and ships it to the relay. The app runs on the device.

**To try the skills before you've got hardware:** visit the [Try it now](https://resident.inanimate.tech/#try-it-now) section of the Resident website, tap the button to connect to the in-browser simulator, and you'll get an 8-character device ID starting with `sim-`. Use it as the device ID:

```
/resident:push-app --device-id sim-xxxxxxxx display Hello, World! on the simulator
```

The simulator emulates an M5StickC Plus2. The plugin bundles that device's surface, so this works with zero local setup — no need to clone the Resident repo.

## Skills

The plugin ships five skills. The first four are the main workflow; the fifth is a sanity check.

### `/resident:write-device-skill`

Interactively author a `DEVICE-SKILL.md` for a new firmware project — the file that documents the device's Lua surface (hardware, modules, examples, constraints). Walks through device identity, driver modules, example apps, and validation stubs. Output is a single Markdown file at the firmware project root that downstream skills (`create-app`, `validate-app`) read to know what Lua surface the device exposes.

### `/resident:create-app`

Generate a Resident Lua app from a natural-language description. Reads the embedded sandbox docs (lifecycle, `ctx`, `log.*`, `time.*`, `kv.*`, math globals) plus the firmware project's `DEVICE-SKILL.md` to know what's available, then composes Lua source. Chains through `validate-app` before reporting done; up to 3 retries if validation fails. Accepts `--device-skill <path>` for an out-of-cwd DEVICE-SKILL.md and `--ref <path>` (repeatable) for additional reference files.

### `/resident:validate-app`

Run a Lua app through a local `lua` interpreter under a permissive stub harness derived from `DEVICE-SKILL.md`. Catches syntax errors, missing lifecycle (`init` / `on_tick` / `on_event`), and obvious runtime bugs (nil dereferences) before pushing to the device. Accepts `--device-skill <path>` and `--ref <path>` for non-standard configurations. Requires `lua` on `PATH` (`brew install lua` if missing).

### `/resident:push-app`

The user-facing entry point. Accepts either a Lua file (pushes directly) or a natural-language description (chains through `create-app` + `validate-app` first, then pushes). Sends via the canonical relay at `resident.inanimate.tech/devices/<deviceId>/send`, or a self-hosted endpoint with `--base-url`. Persists the device ID across invocations via `RESIDENT_DEVICE_ID` env var or a `.resident-device-id` file.

### `/resident:hello-resident`

Trivial liveness check — replies "Resident plugin is alive." Useful for confirming the plugin loaded after install or after editing skill metadata.

## Install (via marketplace)

```
/plugin marketplace add inanimate-tech/agent-plugins
/plugin install resident@inanimate
```

The `inanimate` marketplace tracks `main` of this repo via `git-subdir`, so each install pulls the current contents of `tools/agent-plugin/`.

## Local development

From the root of the resident repo:

```
claude --plugin-dir ./tools/agent-plugin
```

That loads this plugin into the Claude Code session without going through the marketplace.

## Layout

```
tools/agent-plugin/
├── .claude-plugin/
│   └── plugin.json    # Claude Code plugin manifest
├── skills/            # One subdir per skill, each with SKILL.md and tools/
└── README.md
```

## See also

- Marketplace: [`inanimate-tech/agent-plugins`](https://github.com/inanimate-tech/agent-plugins)
- The two-step "bring up a board, then add Resident, then expose Lua modules, then create+push apps" walkthrough: [`docs/start-building.md`](../../docs/start-building.md)
