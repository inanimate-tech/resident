# prompts/ — authoring sheets for coding agents

Model-facing documentation of the Lua surface, maintained NEXT TO the code it
describes so a sheet cannot drift from the module it teaches. These files are
the canonical source; consumers compose them, they don't fork them.

## Composition

Concatenate per what the board actually uses:

1. `sandbox.md` — always. The universal sandbox surface: lifecycle, ctx,
   events, store, log, time, shader globals, limits.
2. `lgfx.md` — when the board registers an `LgfxModule` display.
3. `lvgl.md` — when the board registers the LVGL module. (Arrives with
   `ResidentLvglModule`; not yet written.)
4. The board's `DEVICE-SKILL.md` — the board-specific driver surface
   (sensors, buttons, outputs). Lives in the board firmware project, not
   here.
5. Any framework or device layer on top (e.g. arc's profile authoring
   document: geometry guidance, taste rules).

## Consumers

- The Resident agent plugin's `create-app` skill (its embedded copy carries a
  provenance banner pointing here).
- hawthorn-worker and other hosts that prompt models to write Resident apps.
- Arc hosts, as the base layers of the profile authoring document.

## Editing rules

- A change to a Lua-facing module in `src/` that alters behavior MUST update
  the matching sheet in the same commit.
- Write for a model audience: short declarative sentences, exact names,
  exact limits, one runnable example per module. No marketing, no history.
- Verify every constant against the source before writing it down (the
  sheets state numbers as facts and models will trust them).
