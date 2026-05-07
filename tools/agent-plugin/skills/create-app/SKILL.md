---
name: create-app
description: >-
  Use to generate a Resident Lua app from a natural-language description.
  Reads embedded sandbox docs and the firmware project's DEVICE-SKILL.md
  to know the runtime surface, then writes Lua source to --out or stdout.
  Triggered by /resident:create-app or "write a Lua app for this device".
---

# create-app

Generate a Resident Lua app from a description. Output is plain Lua
source — no validation, no push. The agent composes those steps via the
sibling `validate-app` and `push-app` skills.

## What you need

1. **A description** — one or more sentences from the user about what the
   app should do.
2. **DEVICE-SKILL.md at the firmware project root (cwd)** — describes
   the device-specific Lua module surface. If missing, stop and tell the
   user to run `/resident:write-device-skill` first.
3. **The embedded sandbox docs** — read `docs/sandbox.md` (this skill's
   directory). Covers lifecycle, ctx, log, time, kv, shader globals,
   math globals, constraints.

## Workflow

1. Read `docs/sandbox.md`.
2. Read `./DEVICE-SKILL.md`. If absent → tell the user:
   > "I need a DEVICE-SKILL.md at the firmware project root. Invoke
   > /resident:write-device-skill to author one, then re-run me."
   > Exit without writing anything.
3. Read the user's description.
4. Compose Lua source that:
   - Defines `init`, `on_tick`, and/or `on_event` as appropriate.
   - Uses only the modules and functions shown in DEVICE-SKILL.md and
     `docs/sandbox.md`. Don't invent APIs.
   - Stays small — keep apps tight; long apps risk hitting memory limits.
   - Follows DEVICE-SKILL.md's "Practical Tips" section if present
     (e.g. `screen.flip()` MUST after every draw).
5. Write the result via `tools/write-out.sh`:
   - With `--out path/to/app.lua` → writes the file. Tell the user the path.
   - Without `--out` → prints to stdout.

## Composition

`create-app` does not call `validate-app` or `push-app`. The agent is
expected to compose them after generation:

1. `create-app --out device-apps/foo.lua "<description>"`
2. `validate-app device-apps/foo.lua` — if fails, re-prompt yourself
   (the agent) with the error and regenerate.
3. `push-app --base-url <url> --device-id <id> device-apps/foo.lua`

## Output conventions

- The conventional location for app files in a Resident firmware project
  is `device-apps/<name>.lua`. Suggest this when asking the user for
  an `--out` path; don't create the directory unless the user agrees.
- The user's description is short — DON'T inflate it with comments or
  multi-line docstrings in the generated Lua. Keep generated code tight.
- The user can iterate. If they ask "make it slower / red instead of
  green / etc.", regenerate with the changed brief.
