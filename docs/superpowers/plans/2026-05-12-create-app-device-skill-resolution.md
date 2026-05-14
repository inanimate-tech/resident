# create-app DEVICE-SKILL.md resolution — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Teach `create-app` to accept a caller-provided `DEVICE-SKILL.md` path (and optional reference files), fall back to `./DEVICE-SKILL.md`, and ask the user only as a last resort. Teach `push-app` to forward those flags.

**Architecture:** Pure SKILL.md prose edits — no code, no scripts. Skills in Claude Code are interpreted instructions; the agent does the file resolution via `Read`. Two files touched:
- `tools/agent-plugin/skills/create-app/SKILL.md`
- `tools/agent-plugin/skills/push-app/SKILL.md`

**Tech Stack:** Markdown only.

**Spec:** `docs/superpowers/specs/2026-05-12-create-app-device-skill-resolution-design.md`

**Verification note:** SKILL.md edits aren't unit-testable — the "test" is a careful re-read of the edited sections to confirm the instructions are unambiguous and internally consistent. Each task ends with that re-read step.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `tools/agent-plugin/skills/create-app/SKILL.md` | Tells the agent how to compose Lua apps; lists required inputs and workflow steps. | Update "What you need" + "Workflow" + add an Inputs/example block. |
| `tools/agent-plugin/skills/push-app/SKILL.md` | Tells the agent how to push apps; routes natural-language input through create-app. | Update natural-language workflow to forward `--device-skill` and `--ref`. |

---

## Task 1: Document the new inputs and resolution order in create-app

**Files:**
- Modify: `tools/agent-plugin/skills/create-app/SKILL.md`

Update the "What you need" section to describe the resolution order, then update the "Workflow" steps to implement it, then add a small Inputs section near the top with an example invocation.

- [ ] **Step 1.1: Read the current "What you need" section to confirm exact text**

Read `tools/agent-plugin/skills/create-app/SKILL.md` lines 17–30. You should see the three bullets: description, DEVICE-SKILL.md at cwd, sandbox docs.

- [ ] **Step 1.2: Replace bullet 2 of "What you need"**

Edit `tools/agent-plugin/skills/create-app/SKILL.md`.

Replace:

```
2. **DEVICE-SKILL.md at the firmware project root (cwd)** — describes
   the device-specific Lua module surface. If missing, stop and tell the
   user to run `/resident:write-device-skill` first.
```

with:

```
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
```

- [ ] **Step 1.3: Append a fourth bullet for optional reference files**

In the same "What you need" section, after bullet 3 (the sandbox docs bullet), append:

```
4. **Optional reference files** — the caller may pass one or more
   `--ref <path>` flags (or mention them in natural language: "use the
   example at X as a reference"). Read each before composing — they
   join DEVICE-SKILL.md and the sandbox docs as input. Style and
   patterns are picked up from refs; the user's description still
   drives behavior. No fallback if absent.
```

- [ ] **Step 1.4: Read the modified "What you need" section back**

Read lines 17–50 of `tools/agent-plugin/skills/create-app/SKILL.md`. Confirm:
- Bullet 1 (description) unchanged.
- Bullet 2 now describes the three-step resolution order.
- Bullet 3 (sandbox docs) unchanged.
- Bullet 4 (optional reference files) is present.

- [ ] **Step 1.5: Update Workflow step 2 to match the new resolution order**

In the "## Workflow" section, replace the current step 2:

```
2. Read `./DEVICE-SKILL.md`. If absent → tell the user:
   > "I need a DEVICE-SKILL.md at the firmware project root. Invoke
   > /resident:write-device-skill to author one, then re-run me."
   > Exit without writing anything.
```

with:

```
2. Resolve and read the DEVICE-SKILL.md per the resolution order in
   "What you need":
   - If the caller supplied a path, Read that path.
   - Else if `./DEVICE-SKILL.md` exists, Read it.
   - Else stop and ask:
     > "I need a DEVICE-SKILL.md to know this device's Lua surface.
     > Either tell me where it is (path) or invoke
     > `/resident:write-device-skill` and I'll help you author one."
     > Exit without writing anything.
```

- [ ] **Step 1.6: Insert a new Workflow step to Read any `--ref` files**

Immediately after the (newly rewritten) Workflow step 2, insert a new step. The existing step 3 ("Read the user's description") should remain — the new step slots in front of it and renumbering follows. Make the new step:

```
3. If the caller supplied `--ref <path>` (one or more), Read each file.
   These are extra context — example apps, firmware notes, etc.
```

Renumber the existing steps 3–6 to 4–7.

- [ ] **Step 1.7: Add an Inputs/example block**

After the "## What you need" section and before "## Workflow", insert:

```
## Inputs

Recognised by `/resident:create-app` (also accepted in natural language
by the calling agent):

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
  --ref ./examples/m5stick-demo/device-apps/clock.lua \
  --out device-apps/big-clock.lua \
  "show the time, big digits"
```
```

- [ ] **Step 1.8: Re-read the whole create-app/SKILL.md end-to-end**

Read the full file. Confirm:
- The three-step resolution order appears in both "What you need" (bullet 2) and "Workflow" (step 2).
- Reference files appear in both "What you need" (bullet 4) and "Workflow" (step 3).
- The Inputs section lists `--device-skill`, `--ref`, `--out` and shows an example.
- Step numbers in the Workflow section are contiguous (1, 2, 3, …) with no gaps or duplicates.
- The Composition section at the bottom is unchanged.
- No stale reference to "stop and tell the user to run `/resident:write-device-skill` first" remains as the only fallback — the resolution is now three-step.

- [ ] **Step 1.9: Commit**

```bash
git add tools/agent-plugin/skills/create-app/SKILL.md
git commit -m "feat(create-app): resolve DEVICE-SKILL.md via caller arg, cwd, or prompt"
```

---

## Task 2: Forward the flags from push-app

**Files:**
- Modify: `tools/agent-plugin/skills/push-app/SKILL.md`

push-app's natural-language workflow currently calls `/resident:create-app` with only the description and `--out`. It must also forward `--device-skill` and `--ref` if the caller supplied them.

- [ ] **Step 2.1: Read the current "Workflow — natural-language description" section to confirm exact text**

Read `tools/agent-plugin/skills/push-app/SKILL.md` lines 61–81. You should see the three numbered steps (Generate, Push, Show) and the trailing "If create-app stops with a missing-DEVICE-SKILL.md error..." line.

- [ ] **Step 2.2: Replace step 1 of the natural-language workflow**

Edit `tools/agent-plugin/skills/push-app/SKILL.md`.

Replace:

```
1. **Generate.** Invoke `/resident:create-app` (the sibling skill) with
   the description and an `--out` path under `device-apps/<slug>.lua`.
   That skill embeds the sandbox docs + reads `./DEVICE-SKILL.md` and
   produces tight Lua source. It also chains through
   `/resident:validate-app` automatically; a returned source is already
   compile- and lifecycle-checked.
```

with:

```
1. **Generate.** Invoke `/resident:create-app` (the sibling skill) with
   the description and an `--out` path under `device-apps/<slug>.lua`.
   If the user also passed `--device-skill <path>` and/or one or more
   `--ref <path>` flags to push-app, forward them verbatim to
   create-app. That skill embeds the sandbox docs, resolves
   DEVICE-SKILL.md (caller path → cwd → prompt), reads any `--ref`
   files, and produces tight Lua source. It also chains through
   `/resident:validate-app` automatically; a returned source is already
   compile- and lifecycle-checked.
```

- [ ] **Step 2.3: Replace the trailing "If create-app stops..." paragraph**

Replace:

```
If create-app stops with a missing-DEVICE-SKILL.md error, surface that
error to the user verbatim and stop — don't try to skip validation.
```

with:

```
If create-app stops asking for a DEVICE-SKILL.md path, surface its
prompt to the user verbatim and stop — don't try to skip validation
or guess a path.
```

- [ ] **Step 2.4: Re-read the natural-language workflow section**

Read `tools/agent-plugin/skills/push-app/SKILL.md` lines 60–90. Confirm:
- Step 1 now mentions forwarding `--device-skill` and `--ref` verbatim.
- Step 1 describes the new "caller path → cwd → prompt" resolution.
- The trailing paragraph references the prompt, not a hard error.

- [ ] **Step 2.5: Commit**

```bash
git add tools/agent-plugin/skills/push-app/SKILL.md
git commit -m "feat(push-app): forward --device-skill and --ref to create-app"
```

---

## Task 3: End-to-end sanity check across both skills

**Files:** none modified.

A final cross-file read to catch drift between the two SKILL.md files. SKILL.md drift (e.g. one skill documents a flag the other doesn't recognise) is the most likely failure mode for this change.

- [ ] **Step 3.1: Read create-app/SKILL.md and push-app/SKILL.md side-by-side**

Read both files in full.

- [ ] **Step 3.2: Verify cross-skill consistency**

Confirm by reading:
- create-app accepts `--device-skill <path>` and `--ref <path>` (one or more).
- push-app forwards `--device-skill` and `--ref` to create-app verbatim.
- The "where is it?" prompt wording is consistent between the two files (create-app emits it; push-app refers to it).
- Neither file still says DEVICE-SKILL.md *must* be at `./DEVICE-SKILL.md` — the cwd lookup is one branch of three.
- The example invocation in create-app's Inputs section uses real, plausible paths (`examples/m5stick-demo/DEVICE-SKILL.md` exists in this repo; verify with `ls examples/m5stick-demo/DEVICE-SKILL.md`).

- [ ] **Step 3.3: Verify the example file path actually exists**

Run: `ls examples/m5stick-demo/DEVICE-SKILL.md`
Expected: the file is listed (no error).

If it doesn't exist, change the example in create-app/SKILL.md to use a path that does, and amend Task 1's commit (or add a fixup commit).

- [ ] **Step 3.4: No commit if no changes**

If Step 3.2 or 3.3 surfaced a fix, commit it. Otherwise this task ends without a commit.

---

## Self-Review

**Spec coverage check (against `docs/superpowers/specs/2026-05-12-create-app-device-skill-resolution-design.md`):**

- Resolution order (caller path → cwd → ask) — Task 1 Steps 1.2 and 1.5.
- `--device-skill` flag in caller interface — Task 1 Steps 1.2, 1.7.
- `--ref` flag (repeatable, optional) — Task 1 Steps 1.3, 1.6, 1.7.
- Natural-language form recognised — Task 1 Steps 1.2, 1.3 (both mention the natural-language form).
- Reference files Read alongside sandbox docs — Task 1 Step 1.6.
- push-app pass-through — Task 2 Step 2.2.
- push-app error-surfacing wording updated — Task 2 Step 2.3.
- Cwd lookup still works — preserved in Task 1 Step 1.5 (branch 2 of three).
- No scripts added — confirmed, all tasks are Markdown edits.

No spec gaps found.

**Placeholder scan:** No "TBD", "TODO", or vague "handle appropriately" steps. Each edit step shows the exact old/new text.

**Type consistency:** Flag names (`--device-skill`, `--ref`, `--out`) used identically across both skills. Prompt wording quoted verbatim in both Task 1 and the spec.
