# validate-app: `--ref <path>` flag

## Problem

`validate-app` (at `tools/agent-plugin/skills/validate-app/`) loads
permissive stubs from `DEVICE-SKILL.md` so that locally-run Lua apps
don't crash on calls to device-specific modules (`screen.*`, `imu.*`,
etc.). The stubs come from two extractions:

1. **Module deduction** — `deduce-modules.sh` scans ```lua blocks in
   `DEVICE-SKILL.md` for top-level identifiers like `screen`, then
   builds a permissive-metatable stub for each.
2. **Validation stubs** — an optional `## Validation stubs` section in
   `DEVICE-SKILL.md` provides a Lua block with concrete getter returns
   (e.g. `screen.width()` returns `240`). This appends after the
   auto-deduced stubs, overriding them.

When a user has extended the Resident sandbox with **extra modules**
that aren't in `DEVICE-SKILL.md` — for example, a project-specific
`wifi.*` module documented separately — the validator has no way to
know about them. The catch-all `_G` metatable prevents outright
crashes, but apps that do arithmetic on getter returns from those
modules (e.g. `local n = wifi.signal_strength()`) hit `nil` and fail.

The previous design round (2026-05-12 create-app spec) added a
`--ref <path>` flag to `create-app` and `push-app` so callers can pass
in additional reference files. `validate-app` should accept the same
flag and process those files the same way it processes
`DEVICE-SKILL.md`.

## Design

### New flag in validate.sh

`validate.sh` accepts `--ref <path>` (repeatable). Placed anywhere
before the positional app file:

```bash
validate.sh \
  --device-skill ./DEVICE-SKILL.md \
  --ref ./extensions.md \
  --ref ./more.md \
  app.lua
```

### Format of --ref files

A `--ref` file follows the same conventions as `DEVICE-SKILL.md`:

- ```lua code blocks document the module surface.
- An optional `## Validation stubs` section provides concrete return
  values.

The file extension and overall structure don't matter — the existing
awk-based extraction acts only on ```lua fences and the
`## Validation stubs` marker. A `--ref` file with neither yields no
stubs. This matters because `--ref` in `create-app`'s world is
polymorphic (could be a Markdown skill snippet, could be an example
`.lua` app); in `validate-app`'s pipeline, plain `.lua` files are
silently no-ops, which is the desired behavior.

### Processing order

For each input file in this order:

1. `DEVICE-SKILL.md` (if found via `--device-skill` or cwd fallback).
2. Each `--ref <path>`, in argv order.

For each file, run two extractions:

- **Module deduction** via `deduce-modules.sh` → append no-op
  metatable stubs to `device_stubs`.
- **`## Validation stubs` Lua block** → append to `validation_stubs`.

The final harness composition is unchanged:
```
builtins.lua
<device_stubs>          # all files, concatenated
<validation_stubs>      # all files, concatenated
<fallback_stub>         # the _G catch-all
<user app>
<harness setup>
<harness.lua>
```

Later writes win at runtime (Lua re-assignment). So `--ref` files
appearing after `--device-skill` can override its stubs; among `--ref`
files, later wins. This is consistent with the existing override
behavior (validation_stubs already overrides auto-deduced stubs).

### Chaining from create-app

When `create-app` invokes `/resident:validate-app` after generating
Lua (workflow step 7), it must forward any `--ref <path>` flags it
received. Otherwise apps using modules documented in `--ref` files
will fail validation. `create-app` already reads `--ref` files for
its own composition; forwarding them onward is one extra step.

Chain summary:
- `push-app` forwards `--ref` to `create-app` (existing).
- `create-app` forwards `--ref` to `validate-app` (new).
- `validate-app` consumes them.

### Test fixture

Add one fixture demonstrating the new path:

- `tests/fixtures/extensions.md` — documents a small extra module
  (e.g. `wifi.*` with `signal_strength()` returning a number) and
  contains a `## Validation stubs` block with concrete returns.
- `tests/fixtures/ok-uses-extension.lua` — an app that calls into
  `wifi.signal_strength()` and arithmetic-tests the return.
- `tests/run.sh` — invoke validate.sh with both `--device-skill` and
  `--ref extensions.md` against the new fixture; assert exit 0.

### SKILL.md updates

- `validate-app/SKILL.md`:
  - Add `--ref <path>` to "Usage" examples.
  - Add a "What you need" entry describing the optional flag.
  - Note that ref files use the same DEVICE-SKILL.md conventions and
    that plain .lua refs are silently ignored.
- `create-app/SKILL.md`:
  - In the workflow's validate step, instruct the agent to forward
    any `--ref <path>` flags it received to `/resident:validate-app`.
- `push-app/SKILL.md`: no change. Already forwards `--ref` to
  `create-app`.

## Files touched

- `tools/agent-plugin/skills/validate-app/tools/validate.sh` — accept
  and process `--ref` (repeatable).
- `tools/agent-plugin/skills/validate-app/SKILL.md` — document.
- `tools/agent-plugin/skills/validate-app/tests/fixtures/extensions.md` —
  new.
- `tools/agent-plugin/skills/validate-app/tests/fixtures/ok-uses-extension.lua` —
  new.
- `tools/agent-plugin/skills/validate-app/tests/run.sh` — add a test
  case for the `--ref` path.
- `tools/agent-plugin/skills/create-app/SKILL.md` — forward `--ref`
  to validate-app.
- `tools/agent-plugin/.claude-plugin/plugin.json` — version bump
  (patch; new optional flag is additive).

## Out of scope

- A separate stubs-only file format. Markdown with the existing
  conventions keeps one mental model.
- Filesystem search for ref files (cwd globbing, walking up parents).
  Paths come from the caller.
- Filtering `--ref` flags by extension in `create-app` before
  forwarding. Forward all; validate-app's silent-ignore makes
  non-Markdown refs harmless.
- Changing the catch-all `_G` metatable. The existing fallback is
  still doing useful work for unknown identifiers; `--ref` just
  improves precision for documented extra modules.
