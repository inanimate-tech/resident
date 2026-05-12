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
2. **A DEVICE-SKILL.md** — describes the device-specific Lua module
   surface. Resolved in this order:
   1. **Caller-provided path.** If the invocation supplies a path (via
      `--device-skill <path>` in args, or expressed in natural language
      by the caller — "use the DEVICE-SKILL.md at X"), use it.
   2. **cwd fallback.** Otherwise look for `./DEVICE-SKILL.md`.
   3. **Ask.** If neither resolves, stop and tell the user:
      > "I need a DEVICE-SKILL.md to know this device's Lua surface.
      > Either tell me where it is (path) or invoke
      > `/resident:write-device-skill` and I'll help you author one."
      > Exit without writing.
3. **The embedded sandbox docs** — read
   `${CLAUDE_PLUGIN_ROOT}/skills/create-app/docs/sandbox.md`. Covers
   lifecycle, ctx, log, time, kv, shader globals, math globals,
   constraints. `${CLAUDE_PLUGIN_ROOT}` is set by Claude Code to the
   absolute path of the installed plugin — always use it to reference
   bundled files; the CWD is the user's project, not the skill directory.
4. **Optional reference files** — the caller may pass one or more
   `--ref <path>` flags (or mention them in natural language: "use the
   example at X as a reference"). Read each before composing — they
   join DEVICE-SKILL.md and the sandbox docs as input. Style and
   patterns are picked up from refs; the user's description still
   drives behavior. No fallback if absent.

## Inputs

These may be passed on the slash-command line or expressed in natural
language in the invocation:

- `--device-skill <path>` — DEVICE-SKILL.md anywhere on disk. Optional;
  falls back to `./DEVICE-SKILL.md`, then prompts the user.
- `--ref <path>` — additional reference file. Repeatable. Optional.
- `--out <path>` — where to write the generated Lua. Optional; defaults
  to `device-apps/<short-slug>.lua`.

Followed by the description.

Example:

```bash
/resident:create-app \
  --device-skill ./examples/m5stick-demo/DEVICE-SKILL.md \
  --ref ./examples/m5stick-demo/device-apps/bounce.lua \
  --out device-apps/fast-bounce.lua \
  "make the ball red and twice as fast"
```

## Workflow

1. Read `${CLAUDE_PLUGIN_ROOT}/skills/create-app/docs/sandbox.md`.
2. Resolve and read the DEVICE-SKILL.md per the resolution order in
   "What you need":
   - If the caller supplied a path, Read that path.
   - Else if `./DEVICE-SKILL.md` exists, Read it.
   - Else stop and ask:
     > "I need a DEVICE-SKILL.md to know this device's Lua surface.
     > Either tell me where it is (path) or invoke
     > `/resident:write-device-skill` and I'll help you author one."
     > Exit without writing.
3. If the caller supplied `--ref <path>` (one or more), Read each file.
   These are extra context — example apps, firmware notes, etc.
4. Read the user's description.
5. Compose Lua source that:
   - Defines `init`, `on_tick`, and/or `on_event` as appropriate.
   - Uses only the modules and functions shown in DEVICE-SKILL.md and
     `docs/sandbox.md`. Don't invent APIs.
   - Stays small — keep apps tight; long apps risk hitting memory limits.
   - Follows DEVICE-SKILL.md's "Practical Tips" section if present
     (e.g. `screen.flip()` MUST after every draw).
6. Write the result via
   `${CLAUDE_PLUGIN_ROOT}/skills/create-app/tools/write-out.sh`:
   - With `--out path/to/app.lua` → writes the file. Default to
     `device-apps/<short-slug>.lua` if the user didn't pick a path.
   - Without `--out` → prints to stdout (skip step 7 in that case;
     validation needs a file).
7. **Validate.** Invoke `/resident:validate-app` (the sibling skill) on
   the file you just wrote. If the caller passed any `--ref <path>`
   flags to create-app, forward each one to validate-app as the same
   `--ref <path>` argument — validate-app uses them to stub extra
   modules. It loads the file under a permissive Lua harness and
   checks compile, lifecycle presence, and a few simulated ticks.
   Two outcomes:
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
