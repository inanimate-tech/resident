# validate-app `--ref` flag — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Teach `validate-app` to accept `--ref <path>` (repeatable) so apps that use sandbox extensions documented outside `DEVICE-SKILL.md` can validate cleanly. Teach `create-app` to forward `--ref` flags to validate-app when chaining.

**Architecture:** One bash script (`validate.sh`) gets refactored to apply its existing extraction pipeline (module deduction + `## Validation stubs` block) to every input file in order — `DEVICE-SKILL.md` first, then each `--ref` in argv order. A regression test exercises the new flag. SKILL.md updates document the flag and the create-app→validate-app forwarding contract.

**Tech Stack:** Bash, awk, Lua, Markdown.

**Spec:** `docs/superpowers/specs/2026-05-12-validate-app-ref-flag-design.md`

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `tools/agent-plugin/skills/validate-app/tools/validate.sh` | Entry-point script: arg parsing, stubs assembly, harness composition, run. | Add `--ref` arg parsing; refactor stubs extraction to run per-file over `device_skill + refs`. |
| `tools/agent-plugin/skills/validate-app/tests/run.sh` | Per-fixture regression driver. | Add one new test case exercising `--ref`. |
| `tools/agent-plugin/skills/validate-app/tests/fixtures/extensions.md` | New fixture — minimal Markdown documenting an extra `wifi.*` module with a `## Validation stubs` block. | Create. |
| `tools/agent-plugin/skills/validate-app/tests/fixtures/ok-uses-extension.lua` | New fixture app — calls `wifi.signal_strength()` and does arithmetic on the return. Fails without the ref-supplied stub, passes with it. | Create. |
| `tools/agent-plugin/skills/validate-app/SKILL.md` | Documents validate-app's interface to its caller (the agent). | Document `--ref`. |
| `tools/agent-plugin/skills/create-app/SKILL.md` | Documents create-app's workflow including the validate step. | Instruct the agent to forward `--ref` flags to validate-app. |
| `tools/agent-plugin/.claude-plugin/plugin.json` | Plugin manifest. | Patch bump. |

---

## Task 1: Add `--ref` to validate.sh (TDD)

**Files:**
- Create: `tools/agent-plugin/skills/validate-app/tests/fixtures/extensions.md`
- Create: `tools/agent-plugin/skills/validate-app/tests/fixtures/ok-uses-extension.lua`
- Modify: `tools/agent-plugin/skills/validate-app/tests/run.sh`
- Modify: `tools/agent-plugin/skills/validate-app/tools/validate.sh`

- [ ] **Step 1.1: Read the current validate.sh end-to-end**

Read `tools/agent-plugin/skills/validate-app/tools/validate.sh` in full. Confirm the structure: arg loop with `--device-skill`, environment checks, two stubs extractions (auto-deduced via `deduce-modules.sh`, then `## Validation stubs` block via awk), harness composition, run, exit. You'll be refactoring around the two extractions.

- [ ] **Step 1.2: Create the extensions.md fixture**

Create `tools/agent-plugin/skills/validate-app/tests/fixtures/extensions.md` with this exact content:

```markdown
# Extensions for Test Device

Minimal extension surface for validate-app tests. References one extra
module: `wifi`. Used to test the `--ref` flag.

## Lua Modules

### wifi.*

```lua
local strength = wifi.signal_strength()
local connected = wifi.is_connected()
```

## Validation stubs

```lua
wifi = setmetatable({
  signal_strength = function() return 75 end,
  is_connected   = function() return true end,
}, { __index = function() return function() end end })
```
```

(Note: the file contains literal triple-backtick fences — those are part of the fixture, not Markdown rendering.)

- [ ] **Step 1.3: Create the ok-uses-extension.lua fixture**

Create `tools/agent-plugin/skills/validate-app/tests/fixtures/ok-uses-extension.lua` with this exact content:

```lua
function on_tick(ctx, dt_ms)
  local strength = wifi.signal_strength()
  local pct = strength / 100
  if wifi.is_connected() then
    local _ = pct
  end
end
```

Why this works:
- Without `--ref`: `wifi.signal_strength()` falls through to the `_G` catch-all metatable, which returns a no-op function. Calling it yields `nil`. `nil / 100` is a runtime error. Validate fails.
- With `--ref extensions.md`: `wifi.signal_strength` is bound to `function() return 75 end`. `75 / 100` is fine. Validate passes.

- [ ] **Step 1.4: Add the new test case to run.sh**

In `tools/agent-plugin/skills/validate-app/tests/run.sh`, add a new helper that supports passing refs, and a new test case.

Replace:

```bash
run() {
  local fixture="$1"
  local expected="$2"
  local out err code
  out=$("$VALIDATE" --device-skill "$SKILL" "$fixture" 2>&1)
  code=$?
  if [[ "$expected" == "ok" && "$code" -eq 0 ]]; then
    echo "PASS  $fixture"
    pass=$((pass+1))
  elif [[ "$expected" == "fail" && "$code" -eq 1 ]]; then
    echo "PASS  $fixture (expected fail; exit 1)"
    pass=$((pass+1))
  else
    echo "FAIL  $fixture (expected=$expected, got code=$code)"
    echo "      output: $out"
    fail=$((fail+1))
  fi
}
```

with:

```bash
run() {
  local fixture="$1"
  local expected="$2"
  shift 2
  local extra_args=("$@")
  local out err code
  out=$("$VALIDATE" --device-skill "$SKILL" "${extra_args[@]}" "$fixture" 2>&1)
  code=$?
  if [[ "$expected" == "ok" && "$code" -eq 0 ]]; then
    echo "PASS  $fixture"
    pass=$((pass+1))
  elif [[ "$expected" == "fail" && "$code" -eq 1 ]]; then
    echo "PASS  $fixture (expected fail; exit 1)"
    pass=$((pass+1))
  else
    echo "FAIL  $fixture (expected=$expected, got code=$code)"
    echo "      output: $out"
    fail=$((fail+1))
  fi
}
```

And add this new test case immediately after the existing `run` calls (before the trailing `echo` lines):

```bash
run "$FIXTURES/ok-uses-extension.lua" "ok" --ref "$FIXTURES/extensions.md"
```

Result: the existing five calls still work (no extra_args means no `--ref` is appended), and the new case exercises `--ref`.

- [ ] **Step 1.5: Run the tests, expect the new case to fail**

Run from the repo root:

```bash
bash tools/agent-plugin/skills/validate-app/tests/run.sh
```

Expected: the original five fixtures still PASS, and the new `ok-uses-extension.lua` fails — either with "Unknown option: --ref" (exit 2, which the runner reports as FAIL since it expected `code=0`) or, if the script's catch-all path lets it through without parsing, with a Lua runtime error on `nil / 100`.

The point is to confirm the new test exists and fails *now*, so we can prove the implementation in the next steps makes it pass.

- [ ] **Step 1.6: Refactor stubs extraction in validate.sh and add --ref parsing**

Edit `tools/agent-plugin/skills/validate-app/tools/validate.sh`. Two changes in one pass.

**First change: add `--ref` to the arg loop.**

Replace:

```bash
device_skill=""
app_file=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --device-skill) device_skill="$2"; shift 2 ;;
    -h|--help)
      cat <<EOF
Usage: $0 [--device-skill PATH] [APP_FILE]
       cat app.lua | $0 [--device-skill PATH]

Validates a Resident Lua app locally. Reads from APP_FILE or stdin.
EOF
      exit 0
      ;;
    -*) echo "Unknown option: $1" >&2; exit 2 ;;
    *)  app_file="$1"; shift ;;
  esac
done
```

with:

```bash
device_skill=""
ref_files=()
app_file=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --device-skill) device_skill="$2"; shift 2 ;;
    --ref)          ref_files+=("$2"); shift 2 ;;
    -h|--help)
      cat <<EOF
Usage: $0 [--device-skill PATH] [--ref PATH ...] [APP_FILE]
       cat app.lua | $0 [--device-skill PATH] [--ref PATH ...]

Validates a Resident Lua app locally. Reads from APP_FILE or stdin.
--ref may be passed multiple times. Each ref file is processed the
same way as DEVICE-SKILL.md: module names are auto-deduced from
\`\`\`lua blocks, and a \`## Validation stubs\` Lua block (if any) is
appended after auto-deduced stubs. Plain .lua refs are silently
ignored (no Markdown structure to extract).
EOF
      exit 0
      ;;
    -*) echo "Unknown option: $1" >&2; exit 2 ;;
    *)  app_file="$1"; shift ;;
  esac
done
```

**Second change: refactor the two stubs extractions into a function and call it per-file.**

Replace:

```bash
# Build device-module stubs by deducing module names from DEVICE-SKILL.md.
# Each module gets a permissive metatable.
device_stubs=""
if [[ -n "$device_skill" && -f "$device_skill" ]]; then
  while IFS= read -r module; do
    [[ -z "$module" ]] && continue
    device_stubs+=$'\n'"$module = setmetatable({}, { __index = function() return function() end end })"
  done < <("$DEDUCE" "$device_skill")
fi

# Optional: extract `## Validation stubs` Lua block from DEVICE-SKILL.md.
# Override the auto-deduced loose stubs with concrete return values.
validation_stubs=""
if [[ -n "$device_skill" && -f "$device_skill" ]]; then
  validation_stubs=$(awk '
    /^## Validation stubs/ { in_section = 1; next }
    in_section && /^## /   { in_section = 0; next }
    in_section && /^```lua/ { in_block = 1; next }
    in_section && /^```/    { in_block = 0; next }
    in_section && in_block  { print }
  ' "$device_skill")
fi
```

with:

```bash
# Build stubs by processing each input file (DEVICE-SKILL.md first, then
# each --ref in argv order). For each file:
#   1. Deduce module identifiers from ```lua blocks → permissive metatables.
#   2. Extract `## Validation stubs` Lua block → concrete getter returns.
# Later writes override earlier ones at Lua runtime.
device_stubs=""
validation_stubs=""

process_stubs_file() {
  local file="$1"
  [[ -z "$file" || ! -f "$file" ]] && return 0
  while IFS= read -r module; do
    [[ -z "$module" ]] && continue
    device_stubs+=$'\n'"$module = setmetatable({}, { __index = function() return function() end end })"
  done < <("$DEDUCE" "$file")
  local extracted
  extracted=$(awk '
    /^## Validation stubs/ { in_section = 1; next }
    in_section && /^## /   { in_section = 0; next }
    in_section && /^```lua/ { in_block = 1; next }
    in_section && /^```/    { in_block = 0; next }
    in_section && in_block  { print }
  ' "$file")
  if [[ -n "$extracted" ]]; then
    validation_stubs+=$'\n'"$extracted"
  fi
}

process_stubs_file "$device_skill"
for ref in "${ref_files[@]}"; do
  process_stubs_file "$ref"
done
```

- [ ] **Step 1.7: Run the tests, expect all six to pass**

```bash
bash tools/agent-plugin/skills/validate-app/tests/run.sh
```

Expected: all six fixtures PASS. Final line:

```
6 passed, 0 failed
```

If any fail, read the output to understand which case regressed. The most likely failure modes:
- The five original tests fail → the refactor broke the single-file case. Re-read the diff and check that `process_stubs_file "$device_skill"` matches the previous behavior exactly.
- The new test fails → likely the awk extraction or the function call ordering is wrong. Confirm `ref_files+=("$2"); shift 2` is correct and the loop iterates correctly.

- [ ] **Step 1.8: Commit**

```bash
git add tools/agent-plugin/skills/validate-app/tools/validate.sh \
        tools/agent-plugin/skills/validate-app/tests/run.sh \
        tools/agent-plugin/skills/validate-app/tests/fixtures/extensions.md \
        tools/agent-plugin/skills/validate-app/tests/fixtures/ok-uses-extension.lua
git commit -m "feat(validate-app): accept --ref <path> for extra-module stubs"
```

---

## Task 2: Document `--ref` in validate-app/SKILL.md

**Files:**
- Modify: `tools/agent-plugin/skills/validate-app/SKILL.md`

- [ ] **Step 2.1: Read the current SKILL.md end-to-end**

Read `tools/agent-plugin/skills/validate-app/SKILL.md`. Note the current sections: "What you need", "Usage", "Scope", "Optional: validation stubs in DEVICE-SKILL.md", "Composing with other skills".

- [ ] **Step 2.2: Append a fourth bullet to "What you need"**

In the "## What you need" section, after the existing third bullet ("`lua`"), append:

```
4. **Optional `--ref <path>` flags** — additional Markdown files
   describing extra modules. validate-app processes each ref file the
   same way as DEVICE-SKILL.md: deduces module names from `\`\`\`lua`
   blocks and appends any `## Validation stubs` block. Repeatable.
   Plain `.lua` refs (no Markdown structure) are silently ignored.
   Useful when an app uses sandbox extensions documented outside
   DEVICE-SKILL.md.
```

(The literal triple-backtick is escaped with backslashes only to keep this plan readable; in the actual file it should be a plain ```lua reference.)

- [ ] **Step 2.3: Add a `--ref` usage example**

In the "## Usage" section, after the existing three example commands, add one more line:

```bash
"${CLAUDE_PLUGIN_ROOT}/skills/validate-app/tools/validate.sh" --device-skill path/to/DEVICE-SKILL.md --ref path/to/extensions.md path/to/app.lua
```

(Add it as a fourth line in the same fenced bash block.)

- [ ] **Step 2.4: Re-read the modified file**

Read the SKILL.md again. Confirm:
- "What you need" now has four bullets (1–4) in order.
- Bullet 4 describes `--ref` accurately.
- "Usage" has four example commands, the last using `--ref`.
- Other sections (Scope, Optional, Composing with other skills) untouched.

- [ ] **Step 2.5: Commit**

```bash
git add tools/agent-plugin/skills/validate-app/SKILL.md
git commit -m "docs(validate-app): document --ref flag"
```

---

## Task 3: Forward `--ref` from create-app to validate-app

**Files:**
- Modify: `tools/agent-plugin/skills/create-app/SKILL.md`

- [ ] **Step 3.1: Read create-app/SKILL.md's Workflow section**

Read `tools/agent-plugin/skills/create-app/SKILL.md` lines 68–106. Confirm step 7 is the validate step ("**Validate.** Invoke `/resident:validate-app` (the sibling skill) on the file you just wrote...").

- [ ] **Step 3.2: Update step 7 to forward --ref flags**

Replace the opening sentence of step 7:

```
7. **Validate.** Invoke `/resident:validate-app` (the sibling skill) on
   the file you just wrote. It loads the file under a permissive Lua
   harness and checks compile, lifecycle presence, and a few simulated
   ticks. Two outcomes:
```

with:

```
7. **Validate.** Invoke `/resident:validate-app` (the sibling skill) on
   the file you just wrote. If the caller passed any `--ref <path>`
   flags to create-app, forward each one to validate-app as the same
   `--ref <path>` argument — validate-app uses them to stub extra
   modules. It loads the file under a permissive Lua harness and
   checks compile, lifecycle presence, and a few simulated ticks.
   Two outcomes:
```

Leave the rest of step 7 (PASS/FAIL branches and retry policy) unchanged.

- [ ] **Step 3.3: Re-read the modified file**

Read the modified section. Confirm:
- Step 7's opening paragraph now mentions forwarding `--ref` flags.
- The PASS/FAIL branches and 3-retry policy are intact.
- No other step changed.

- [ ] **Step 3.4: Commit**

```bash
git add tools/agent-plugin/skills/create-app/SKILL.md
git commit -m "feat(create-app): forward --ref flags to validate-app"
```

---

## Task 4: Bump plugin version

**Files:**
- Modify: `tools/agent-plugin/.claude-plugin/plugin.json`

- [ ] **Step 4.1: Read the current plugin.json**

Confirm current version. From recent history it should be `0.6.2`.

- [ ] **Step 4.2: Bump to 0.6.3**

Edit `tools/agent-plugin/.claude-plugin/plugin.json`. Change `"version": "0.6.2"` to `"version": "0.6.3"`. This is a patch bump consistent with the previous round — the new flag is optional and backwards-compatible.

- [ ] **Step 4.3: Commit**

```bash
git add tools/agent-plugin/.claude-plugin/plugin.json
git commit -m "$(cat <<'EOF'
chore(agent-plugin): bump to 0.6.3

validate-app accepts --ref for extra-module stubs; create-app forwards
--ref flags through to validate-app when chaining.
EOF
)"
```

---

## Self-Review

**Spec coverage check (against `docs/superpowers/specs/2026-05-12-validate-app-ref-flag-design.md`):**

- New `--ref <path>` flag on validate.sh — Task 1, Step 1.6.
- Repeatable (`--ref ... --ref ...`) — Task 1, Step 1.6 (`ref_files+=("$2")` accumulates).
- Same Markdown conventions as DEVICE-SKILL.md — Task 1, Step 1.6 (`process_stubs_file` calls the same `deduce-modules.sh` and the same awk script).
- Processing order DEVICE-SKILL.md first, then refs in argv order — Task 1, Step 1.6 (explicit ordering in the `process_stubs_file` calls).
- Later writes win — Task 1, Step 1.6 (Lua re-assignment; documented in the script comment).
- Plain .lua refs silently no-op — Task 1, Step 1.6 (awk extraction yields empty; documented in `--help`).
- Test fixture with extra module + validation stubs — Task 1, Steps 1.2, 1.3.
- Test in run.sh — Task 1, Step 1.4.
- validate-app SKILL.md documents `--ref` — Task 2.
- create-app forwards `--ref` — Task 3.
- push-app no change — confirmed (no task).
- Version bump (patch) — Task 4.

No spec gaps.

**Placeholder scan:** No "TBD", no "appropriate error handling", no "similar to Task N". Each edit step shows exact old/new text. Each test step shows exact expected output.

**Type consistency:** Flag name `--ref` used identically throughout. Variable `ref_files` used consistently. Function `process_stubs_file` defined and called consistently. Fixture file paths (`tests/fixtures/extensions.md`, `tests/fixtures/ok-uses-extension.lua`) used identically across Steps 1.2, 1.3, 1.4.
