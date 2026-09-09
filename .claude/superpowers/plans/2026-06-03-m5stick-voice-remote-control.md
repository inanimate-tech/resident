# m5stick-voice Remote Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Say "ok push app" in the m5stick-voice viewer and have the server push the simulator's current Lua app to the physical device, which suspends any running app while in push-to-talk "Listening" mode and resumes it on release.

**Architecture:** Add a small app-suspend primitive to `Resident::Sandbox` (pause/resume the Lua tick without unloading), wire the device's push-to-talk handler to it, add a `repaint()` to the shared DisplayDriver so the app's last frame is restored on resume, and add a `push_app` realtime function tool to the server's `VoiceAgent` that sends `{type:"app", code}` to the device connection. Bump the library to `0.5.1-dev`.

**Tech Stack:** C++17 / Arduino / PlatformIO (device + library), Esp32Lua, TanStack Start + Cloudflare Agents SDK + Vitest (server).

---

## Testing posture (read first)

- **Firmware/library (Tasks 1–3, 5):** the `test/unit` native harness cannot construct a `Resident::Sandbox` (no native stubs for Courier::Client / ezTime / ArduinoJson; no test instantiates `Sandbox`). Per the project's firmware-verify rule, these changes are verified by the **build gate** + **on-hardware test** (Task 7), not new unit tests. This is a deliberate, approved decision (see the spec).
- **Server (Task 4):** verified by `typecheck` + `build` + the existing Vitest suite staying green. `push_app` is I/O glue over the Agents SDK connection API, which has no DO test harness here, so no contrived unit test is added.
- **Commits:** the skill's "frequent commits" is overridden by the user's firmware-verify rule (user instructions win). The **server** change commits after its checks pass (Task 4). All **firmware/library** changes are staged but **held uncommitted** until the on-hardware checkpoint (Task 7), then committed in Task 8.

## Pre-flight (existing uncommitted state)

`git status` currently shows, in addition to this feature's future edits:
- `examples/m5stick-voice/device/src/main.cpp` — the deviceId-under-prompt edit (already hardware-verified: "firmware looks fine"). It will ride along with this feature's `main.cpp` changes in the Task 8 firmware commit.
- `CLAUDE.md`, `.gitignore` — the Superpowers-working-docs policy change. These are unrelated to the feature; recommend committing them on their own:
  ```bash
  git add CLAUDE.md .gitignore
  git commit -m "docs: route Superpowers working docs to .claude/superpowers/"
  ```
  Do this whenever convenient — not gated by hardware.

## File structure

- `src/ResidentSandbox.h` — declare `suspendApp()`/`resumeApp()`/`isAppSuspended()` + `_appSuspended` member.
- `src/ResidentSandbox.cpp` — implement them; gate the `loop()` tick; clear the flag in `loadApp()`.
- `examples/m5stick-demo/device/lib/drivers/src/DisplayDriver.h` / `.cpp` — add `repaint()` (shared by m5stick-demo + m5stick-voice).
- `examples/m5stick-voice/device/src/main.cpp` — suspend on talk, resume+repaint on release.
- `examples/m5stick-voice/server/src/agents/voice-agent.ts` — `push_app` tool, handler, system-prompt line, `DEFAULT_APP` import.
- `library.json`, `idf_component.yml`, `docs/changelog.md` — version bump + changelog.

---

### Task 1: Library — app-suspend primitive

**Files:**
- Modify: `src/ResidentSandbox.h`
- Modify: `src/ResidentSandbox.cpp`

- [ ] **Step 1: Declare the public methods**

In `src/ResidentSandbox.h`, replace:

```cpp
    // State queries
    bool isAppRunning() const;

    // Timezone — no-op on nullptr/empty. Success means ezTime resolved the
```

with:

```cpp
    // State queries
    bool isAppRunning() const;

    // App suspend/resume. Pauses the Lua tick (on_tick + event dispatch)
    // without unloading the app — Courier and extension update() keep running.
    // While suspended the status display is freed (notifyAppRunning(false)) so
    // displayText() can show e.g. a "Listening" overlay. Both are no-ops when
    // no app is loaded. isAppRunning() stays true while suspended; suspension
    // is a separate axis queried via isAppSuspended().
    void suspendApp();
    void resumeApp();
    bool isAppSuspended() const;

    // Timezone — no-op on nullptr/empty. Success means ezTime resolved the
```

- [ ] **Step 2: Declare the private member**

In `src/ResidentSandbox.h`, replace:

```cpp
    struct lua_State* _lua = nullptr;
    bool _appRunning = false;
```

with:

```cpp
    struct lua_State* _lua = nullptr;
    bool _appRunning = false;
    bool _appSuspended = false;
```

- [ ] **Step 3: Gate the tick in loop()**

In `src/ResidentSandbox.cpp`, replace:

```cpp
  // Lua tick + event dispatch only when an app is running.
  if (!_appRunning) return;
```

with:

```cpp
  // Lua tick + event dispatch only when an app is running and not suspended.
  if (!_appRunning || _appSuspended) return;
```

- [ ] **Step 4: Clear the flag in loadApp()**

In `src/ResidentSandbox.cpp`, replace:

```cpp
void Sandbox::loadApp(const char* luaCode)
{
  // Stop current app before loading new one
  if (_appRunning) {
```

with:

```cpp
void Sandbox::loadApp(const char* luaCode)
{
  // A freshly loaded app starts running, never suspended.
  _appSuspended = false;

  // Stop current app before loading new one
  if (_appRunning) {
```

- [ ] **Step 5: Implement the three methods**

In `src/ResidentSandbox.cpp`, replace:

```cpp
bool Sandbox::isAppRunning() const
{
  return _appRunning;
}

// --- Lua compilation ---
```

with:

```cpp
bool Sandbox::isAppRunning() const
{
  return _appRunning;
}

void Sandbox::suspendApp()
{
  if (!_appRunning || _appSuspended) return;
  _appSuspended = true;
  notifyAppRunning(false);  // free the status display for overlay text
}

void Sandbox::resumeApp()
{
  if (!_appRunning || !_appSuspended) return;
  _appSuspended = false;
  notifyAppRunning(true);   // re-suppress status display; app owns the screen
}

bool Sandbox::isAppSuspended() const
{
  return _appSuspended;
}

// --- Lua compilation ---
```

- [ ] **Step 6: Verify it compiles into firmware**

Run: `cd examples/m5stick-voice/device && pio run -e m5stick`
Expected: `[SUCCESS]` (links `firmware.elf`). ~10s.

- [ ] **Step 7: Verify the native suite is unaffected**

Run: `cd /Users/matt/code/resident && ./tools/run-tests.py unit`
Expected: all tests pass (no Sandbox tests exist; this confirms the header/impl change didn't break the config/lua tests).

> Do NOT commit yet — firmware/library changes are held until the Task 7 hardware checkpoint.

---

### Task 2: Shared DisplayDriver — repaint()

**Files:**
- Modify: `examples/m5stick-demo/device/lib/drivers/src/DisplayDriver.h`
- Modify: `examples/m5stick-demo/device/lib/drivers/src/DisplayDriver.cpp`

- [ ] **Step 1: Declare repaint()**

In `examples/m5stick-demo/device/lib/drivers/src/DisplayDriver.h`, replace:

```cpp
  // Call once after M5.begin() to create the sprite framebuffer
  void begin() override;
```

with:

```cpp
  // Call once after M5.begin() to create the sprite framebuffer
  void begin() override;

  // Re-push the current off-screen sprite to the display without redrawing it.
  // Restores the last app frame after a direct displayText() overlay (e.g. the
  // m5stick-voice "Listening" prompt) so static apps don't stay blank on resume.
  void repaint();
```

- [ ] **Step 2: Implement repaint()**

In `examples/m5stick-demo/device/lib/drivers/src/DisplayDriver.cpp`, replace:

```cpp
void DisplayDriver::onAppReset() {
```

with:

```cpp
void DisplayDriver::repaint() {
  if (_initialized) _canvas.pushSprite(0, 0);
}

void DisplayDriver::onAppReset() {
```

- [ ] **Step 3: Verify it compiles**

Run: `cd examples/m5stick-voice/device && pio run -e m5stick`
Expected: `[SUCCESS]`.

> Do NOT commit yet.

---

### Task 3: Device example — wire push-to-talk to suspend/resume

**Files:**
- Modify: `examples/m5stick-voice/device/src/main.cpp`

- [ ] **Step 1: Suspend the app when a talk hold starts**

In `examples/m5stick-voice/device/src/main.cpp`, replace:

```cpp
        dbgHoldStarts++;
        streaming = true;
        displayDriver.displayText("Listening");
```

with:

```cpp
        dbgHoldStarts++;
        streaming = true;
        if (sandbox.isAppRunning()) sandbox.suspendApp();
        displayDriver.displayText("Listening");
```

- [ ] **Step 2: Resume the app (or show the idle prompt) on release**

In `examples/m5stick-voice/device/src/main.cpp`, replace:

```cpp
    } else {
        streaming = false;
        showIdlePrompt();
        Serial.printf("[voice] %lu HOLD end -> stopped "
```

with:

```cpp
    } else {
        streaming = false;
        if (sandbox.isAppRunning()) {
            sandbox.resumeApp();
            displayDriver.repaint();  // restore the app's last frame immediately
        } else {
            showIdlePrompt();
        }
        Serial.printf("[voice] %lu HOLD end -> stopped "
```

- [ ] **Step 3: Verify it compiles**

Run: `cd examples/m5stick-voice/device && pio run -e m5stick`
Expected: `[SUCCESS]`.

> Do NOT commit yet — firmware changes wait for Task 7.

---

### Task 4: Server — push_app realtime tool

**Files:**
- Modify: `examples/m5stick-voice/server/src/agents/voice-agent.ts`

- [ ] **Step 1: Import the default app**

In `voice-agent.ts`, replace:

```ts
import { validateLuaCode } from "../lib/lua-validator"
```

with:

```ts
import { validateLuaCode } from "../lib/lua-validator"
import { DEFAULT_APP } from "../lib/default-app"
```

- [ ] **Step 2: Describe push_app in the system prompt**

In `voice-agent.ts`, replace:

```ts
2. **create_app** — generate and run a Lua app on the SIMULATED M5StickC DEVICE shown on the page (a small 240×135 screen with two buttons). Use when the user asks for something to happen "on the device", "on the m5stick", "on the screen", asks for a clock, a counter, a game, a bouncing ball, anything interactive. Returns asynchronously — the coding agent writes Lua and pushes it; the user sees status in the UI.

For ambiguous requests like "show stripes", default to apply_css (the page) unless the user mentioned the device. Prefer acting through a tool over talking; keep spoken replies brief.`
```

with:

```ts
2. **create_app** — generate and run a Lua app on the SIMULATED M5StickC DEVICE shown on the page (a small 240×135 screen with two buttons). Use when the user asks for something to happen "on the device", "on the m5stick", "on the screen", asks for a clock, a counter, a game, a bouncing ball, anything interactive. Returns asynchronously — the coding agent writes Lua and pushes it; the user sees status in the UI.

3. **push_app** — push the app CURRENTLY SHOWN IN THE SIMULATOR to the user's PHYSICAL device. Use when the user says to push / send / deploy / load the app onto the device, the stick, or the hardware (e.g. "ok push app", "send it to my stick"). No arguments — it sends whatever the simulator is showing.

For ambiguous requests like "show stripes", default to apply_css (the page) unless the user mentioned the device. Prefer acting through a tool over talking; keep spoken replies brief.`
```

- [ ] **Step 3: Add the push_app tool to the session**

In `voice-agent.ts`, replace:

```ts
                required: ["description"],
              },
            },
          ],
          tool_choice: "auto",
```

with:

```ts
                required: ["description"],
              },
            },
            {
              type: "function",
              name: "push_app",
              description:
                "Push the app currently shown in the simulator to the PHYSICAL device over its WebSocket. Use when the user says to push / send / deploy / load the app onto the device / stick / hardware. No arguments.",
              parameters: { type: "object", properties: {} },
            },
          ],
          tool_choice: "auto",
```

- [ ] **Step 4: Route the tool call**

In `voice-agent.ts`, replace:

```ts
    if (name === "apply_css") {
      this.handleApplyCss(callId, argsJson)
      return
    }
    if (name !== "create_app") {
```

with:

```ts
    if (name === "apply_css") {
      this.handleApplyCss(callId, argsJson)
      return
    }
    if (name === "push_app") {
      this.handlePushApp(callId)
      return
    }
    if (name !== "create_app") {
```

- [ ] **Step 5: Implement handlePushApp**

In `voice-agent.ts`, replace:

```ts
  private sendToolResult(callId: string, payload: unknown): void {
```

with:

```ts
  private handlePushApp(callId: string): void {
    // "The app in the sim" is currentApp (set by create_app), or the default
    // bouncing ball the viewer renders when nothing has been generated yet.
    const code = this.currentApp?.code ?? DEFAULT_APP
    const frame = JSON.stringify({ type: "app", code })
    const devices = Array.from(this.getConnections("device"))
    for (const d of devices) d.send(frame)
    console.log("[voice] push_app ->", devices.length, "device(s),", code.length, "chars")
    this.sendToolResult(
      callId,
      devices.length > 0
        ? { ok: true, devices: devices.length }
        : { ok: false, error: "no device connected" },
    )
    if (this.openai && this.openaiReady) {
      this.openai.send(JSON.stringify({ type: "response.create" }))
    }
  }

  private sendToolResult(callId: string, payload: unknown): void {
```

- [ ] **Step 6: Typecheck, build, test**

Run: `cd examples/m5stick-voice/server && npm run typecheck && npm run build && npm test`
Expected: `tsc` clean; vite build succeeds (client + ssr); Vitest `5 passed`.

- [ ] **Step 7: Commit the server change**

```bash
cd /Users/matt/code/resident
git add examples/m5stick-voice/server/src/agents/voice-agent.ts
git commit -m "feat(m5stick-voice): push_app realtime tool sends the sim's app to the device

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Version bump + changelog

**Files:**
- Modify: `library.json`
- Modify: `idf_component.yml`
- Modify: `docs/changelog.md`

- [ ] **Step 1: Bump library.json**

In `library.json`, replace:

```json
  "version": "0.5.0",
```

with:

```json
  "version": "0.5.1-dev",
```

- [ ] **Step 2: Bump idf_component.yml**

In `idf_component.yml`, replace:

```yaml
version: "0.5.0"
```

with:

```yaml
version: "0.5.1-dev"
```

- [ ] **Step 3: Add the changelog section**

In `docs/changelog.md`, replace:

```markdown
# Changelog

## v0.5.0

First public alpha.
```

with:

```markdown
# Changelog

## v0.5.1-dev (<git-hash>)

### New features

- `Resident::Sandbox::suspendApp()` / `resumeApp()` / `isAppSuspended()` — pause
  and resume a running app's tick without unloading it. While suspended,
  `loop()` skips the Lua `on_tick`/event dispatch (Courier and extension updates
  keep running) and the status display is freed for direct text via
  `StatusDisplay::displayText()`. The m5stick-voice example uses it to show
  "Listening" over a running app during push-to-talk.

## v0.5.0

First public alpha.
```

> `<git-hash>` is filled in Task 8 (after the firmware commit exists). Do NOT commit yet.

---

### Task 6: Full pre-hardware test sweep

- [ ] **Step 1: Run the firmware test suite**

Run: `cd /Users/matt/code/resident && ./tools/run-tests.py unit static-analysis build`
Expected: unit tests pass; cppcheck clean; all example PlatformIO envs build `[SUCCESS]`.

If cppcheck flags the new methods, address it before proceeding (the additions are simple enough that none is expected).

---

### Task 7: Hardware verification checkpoint (Matt)

> This is a manual gate. Do not proceed to commit (Task 8) until Matt confirms.

- [ ] **Step 1: Flash the device**

Run: `cd examples/m5stick-voice/device && pio run -e m5stick -t upload -t monitor`

- [ ] **Step 2: End-to-end check** — confirm each:
  - Open `https://<worker-host>/devices/<deviceId>/`, ask the agent to make an app (e.g. "make a bouncing ball on the device"); it appears in the sim.
  - Hold the device button, say "ok push app", release. The app appears **on the stick**.
  - With the app running on the stick, **hold to talk**: the app freezes and "Listening" shows.
  - **Release**: the app reappears (restored frame) and resumes animating.
  - Repeat with a *static* app (e.g. a clock or text) to confirm `repaint()` restores it on release.
  - With **no** app generated yet, "ok push app" pushes the default bouncing ball.

- [ ] **Step 2: Get Matt's explicit go-ahead** before committing.

---

### Task 8: Commit the firmware/library changes

> Only after Task 7 passes.

- [ ] **Step 1: Stage and commit the firmware bundle**

```bash
cd /Users/matt/code/resident
git add src/ResidentSandbox.h src/ResidentSandbox.cpp \
        examples/m5stick-demo/device/lib/drivers/src/DisplayDriver.h \
        examples/m5stick-demo/device/lib/drivers/src/DisplayDriver.cpp \
        examples/m5stick-voice/device/src/main.cpp \
        library.json idf_component.yml docs/changelog.md
git commit -m "feat(resident): Sandbox suspendApp/resumeApp; m5stick-voice suspends app while listening

Adds Resident::Sandbox::suspendApp()/resumeApp()/isAppSuspended() to pause a
running app's tick without unloading it, and DisplayDriver::repaint() to restore
the last frame. m5stick-voice suspends the running app and shows \"Listening\"
during push-to-talk, resuming on release. Bumps library to 0.5.1-dev.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 2: Fill the changelog git-hash and amend**

```bash
HASH=$(git rev-parse --short HEAD)
# Replace the literal <git-hash> placeholder in the new changelog heading:
sed -i '' "s/## v0.5.1-dev (<git-hash>)/## v0.5.1-dev ($HASH)/" docs/changelog.md
git add docs/changelog.md
git commit --amend --no-edit
```

- [ ] **Step 3: Confirm clean tree**

Run: `git status --short`
Expected: no tracked changes remain from this feature (the `.claude/superpowers/` spec + plan are git-ignored and won't appear).

---

## Self-review

- **Spec coverage:** Component 1 → Task 1; Component 2 → Task 2; Component 3 → Task 3; Component 4 → Task 4; Component 5 → Task 5. Verification → Tasks 6–7. ✅
- **No-app-yet** (push `DEFAULT_APP`) → Task 4 Step 5. **Resume-in-place** (no `init()` rerun) → suspend/resume only toggle a flag (Task 1). **repaint()** → Tasks 2/3. ✅
- **Type/name consistency:** `suspendApp`/`resumeApp`/`isAppSuspended`/`_appSuspended` used identically across Tasks 1 and 3; `repaint()` declared (Task 2) and called (Task 3); `handlePushApp`/`DEFAULT_APP`/`getConnections("device")` consistent within Task 4. ✅
- **No placeholders:** every code step shows exact text; the only literal placeholder is the changelog `<git-hash>`, intentionally resolved in Task 8 Step 2. ✅
