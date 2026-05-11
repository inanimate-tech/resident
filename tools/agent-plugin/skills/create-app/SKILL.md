---
name: create-app
description: >-
  Use to generate a Resident Lua app from a natural-language description.
  Reads embedded sandbox docs and the firmware project's DEVICE-SKILL.md
  to know the runtime surface, generates Lua source, validates it, and
  writes the result via --out (or stdout). Triggered by
  /resident:create-app or "write a Lua app for this device".
---

# create-app

Generate a Resident Lua app from a description. The output is validated
Lua source — `create-app` chains through `validate-app` before reporting
done. It does NOT push to a device; that's `push-app`'s job.

## What you need

1. **A description** — one or more sentences from the user about what the
   app should do.
2. **DEVICE-SKILL.md at the firmware project root (cwd)** — describes
   the device-specific Lua module surface. If missing, stop and tell the
   user to run `/resident:write-device-skill` first.
3. **The embedded sandbox docs** — read
   `${CLAUDE_PLUGIN_ROOT}/skills/create-app/docs/sandbox.md`. Covers
   lifecycle, ctx, log, time, kv, shader globals, math globals,
   constraints. `${CLAUDE_PLUGIN_ROOT}` is set by Claude Code to the
   absolute path of the installed plugin — always use it to reference
   bundled files; the CWD is the user's project, not the skill directory.

## Workflow

1. Read `${CLAUDE_PLUGIN_ROOT}/skills/create-app/docs/sandbox.md`.
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
5. Write the result via
   `${CLAUDE_PLUGIN_ROOT}/skills/create-app/tools/write-out.sh`:
   - With `--out path/to/app.lua` → writes the file. Default to
     `device-apps/<short-slug>.lua` if the user didn't pick a path.
   - Without `--out` → prints to stdout (skip step 6 in that case;
     validation needs a file).
6. **Validate.** Invoke `/resident:validate-app` (the sibling skill) on
   the file you just wrote. It loads the file under a permissive Lua
   harness and checks compile, lifecycle presence, and a few simulated
   ticks. Two outcomes:
   - **PASS:** report the file path and a tight summary of what the app
     does. Done.
   - **FAIL:** the validator stderr names a line and a Lua error. Read
     the error, fix the Lua source (regenerate from the original
     description plus the error context), write again, and re-validate.
     Up to 3 retries; if it still fails, surface the most recent error
     to the user and stop.

## Output conventions

- The conventional location for app files in a Resident firmware project
  is `device-apps/<name>.lua`. Default to that when the user didn't
  pass `--out`.
- The user's description is short — DON'T inflate it with comments or
  multi-line docstrings in the generated Lua. Keep generated code tight.
- The user can iterate. If they ask "make it slower / red instead of
  green / etc.", regenerate with the changed brief.

## Composition with push-app

`push-app` is the user-facing entry point that accepts either a Lua file
(push directly) or a description (which it routes through `create-app`).
When the user invokes `/resident:create-app` directly, you don't push —
they want the source for inspection or downstream use.
