# create-app: DEVICE-SKILL.md resolution

## Problem

The `create-app` skill (in the `resident` Claude Code plugin at
`tools/agent-plugin/skills/create-app/`) currently looks for
`DEVICE-SKILL.md` only at `./DEVICE-SKILL.md`. If that file is missing,
the skill stops and tells the user to run `/resident:write-device-skill`.

Three scenarios hit that dead end today:

1. The user invoked from a sub-directory of a firmware project (the file
   exists, just not at `./`).
2. The user wants to target a known device (e.g. the seeded M5StickC
   example at `examples/m5stick-demo/DEVICE-SKILL.md`) from outside that
   project's root.
3. The user hasn't authored a DEVICE-SKILL.md yet for their project.

Only case 3 is what the current error message addresses. The skill
should handle all three.

## Design

### Resolution order

When `create-app` runs, resolve the DEVICE-SKILL.md path in this order:

1. **Caller-provided path.** If the invocation includes a path to a
   DEVICE-SKILL.md (via `--device-skill <path>` in args, or expressed in
   natural language by the calling agent — "use the DEVICE-SKILL.md at
   X"), use it.
2. **cwd fallback.** Otherwise look for `./DEVICE-SKILL.md` (current
   behavior).
3. **Ask.** If neither resolves, stop and ask the user:

   > "I need a DEVICE-SKILL.md to know this device's Lua surface. Either
   > tell me where it is (path) or invoke `/resident:write-device-skill`
   > and I'll help you author one."

   Exit without writing any Lua.

### Caller interface

Skills in Claude Code receive a free-form args string. The SKILL.md
documents two recognized inputs that the agent should look for, in
either flag form or in natural language:

- `--device-skill <path>` — path to DEVICE-SKILL.md (anywhere on disk).
- `--ref <path>` — additional reference file (example app, firmware
  note, etc.). Repeatable. No fallback if absent — these are purely
  optional extra context.

…followed by the description.

Example:

```
/resident:create-app \
  --device-skill ./examples/m5stick-demo/DEVICE-SKILL.md \
  --ref ./examples/m5stick-demo/device-apps/clock.lua \
  "show the time, big digits"
```

### Reference files

If the caller passed `--ref <path>` (one or more times), the skill Reads
each file before composing. These join DEVICE-SKILL.md and the embedded
sandbox docs as input to the compose step. They influence style and
provide patterns to crib from; the user's description still drives
behavior.

No prompting if `--ref` is absent — it's optional.

### push-app composition

`push-app`'s natural-language workflow currently doesn't pass any
pointer through to create-app. Update `push-app`'s SKILL.md to:

- Accept the same `--device-skill` and `--ref` flags before its
  description.
- Forward them verbatim when invoking `/resident:create-app`.
- The existing "if create-app errors, surface verbatim and stop" line
  already covers the new "where is it?" prompt.

### What does NOT change

- The cwd lookup at `./DEVICE-SKILL.md` (case 2 above) still works for
  users who invoke from the project root — no behavior change for the
  common path.
- The validation step (chaining through `/resident:validate-app`) is
  untouched.
- Output conventions (`device-apps/<slug>.lua` default for `--out`) are
  untouched.
- The sandbox docs at
  `${CLAUDE_PLUGIN_ROOT}/skills/create-app/docs/sandbox.md` are still
  always loaded; that's plugin-bundled.
- No scripts or executables added. This is interpreted SKILL.md
  instructions only — the agent does the resolution via Read.

## Files touched

- `tools/agent-plugin/skills/create-app/SKILL.md` — update "What you
  need" and "Workflow" sections; document the resolution order and the
  two new caller inputs.
- `tools/agent-plugin/skills/push-app/SKILL.md` — document the
  pass-through flags in the natural-language workflow.

## Out of scope

- Searching the filesystem (walking up parent dirs, scanning
  `examples/*/DEVICE-SKILL.md`) — explicit inputs only.
- A bundled "generic" DEVICE-SKILL.md fallback — apps without a device
  module surface aren't interesting.
- Any change to `write-device-skill` itself.
