# Device Agent-Status Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Have the physical M5Stick render coding-job status directly from the `agent_status` WebSocket frames, replacing the server-pushed `WORKING_APP` Lua placeholder.

**Architecture:** Delete the server's `WORKING_APP` device push (the `agent_status` stream already reaches devices). On the device, register `sandbox.onMessage` to render generic status text via `displayText` — suspending a running app on the first `working` frame so the text isn't gated off — and add a tap-to-recover on error using `getTotalPressCount()` (only taps bump it, not push-to-talk holds).

**Tech Stack:** TypeScript / Cloudflare Worker (server); C++ / PlatformIO / Arduino-ESP32, ArduinoJson, M5Unified (device firmware). Spec: `.claude/superpowers/specs/2026-06-10-device-agent-status-display-design.md`.

---

## File structure

- **Modify** `examples/m5stick-voice/server/src/agents/voice-agent.ts` — remove the `WORKING_APP` push in `handleFunctionCall` and the `WORKING_APP` import.
- **Modify** `examples/m5stick-voice/server/src/lib/default-app.ts` — delete the now-unused `WORKING_APP` export (keep `DEFAULT_APP`).
- **Modify** `examples/m5stick-voice/device/src/main.cpp` — add `agent_status` handling (state + `onMessage` + tap-recover in `loop()`).

The firmware has no unit-test harness (Arduino/ESP32 C++); the device task is verified by compiling both PlatformIO envs and by an on-hardware check. Per repo policy, **the device commit is gated on a real-hardware test** — it is not committed on a compile pass alone.

---

## Task 1: Server — drop the `WORKING_APP` placeholder push

**Files:**
- Modify: `examples/m5stick-voice/server/src/agents/voice-agent.ts` (import line 6; `handleFunctionCall` lines 320-323)
- Modify: `examples/m5stick-voice/server/src/lib/default-app.ts:31-44`

This is a deletion; no new test. It is verified by `typecheck` / existing `test` / `build` staying green. Run all commands from `examples/m5stick-voice/server`.

- [ ] **Step 1: Remove the device placeholder push**

In `src/agents/voice-agent.ts`, delete the push block in `handleFunctionCall`. Change:

```ts
    // Tell the viewer + device: started (zero lines so far).
    this.setAgentStatus("working", { lines: 0 })

    // Immediately show a "Working..." placeholder on the physical device while
    // the coding agent generates the real app — regardless of any monitor.
    const working = this.pushAppToDevices(WORKING_APP)
    if (working > 0) console.log("[voice] pushed Working... ->", working, "device(s)")

    // Fire-and-forget.
    this.ctx.waitUntil(this.runCodingJob(jobId, parsed.description, this.codingAbort.signal))
```

to:

```ts
    // Tell the viewer + device: started (zero lines so far). The device renders
    // its own status from this agent_status stream — no placeholder app push.
    this.setAgentStatus("working", { lines: 0 })

    // Fire-and-forget.
    this.ctx.waitUntil(this.runCodingJob(jobId, parsed.description, this.codingAbort.signal))
```

- [ ] **Step 2: Drop the now-unused `WORKING_APP` import**

In `src/agents/voice-agent.ts`, change the import (line 6):

```ts
import { DEFAULT_APP, WORKING_APP } from "../lib/default-app"
```

to:

```ts
import { DEFAULT_APP } from "../lib/default-app"
```

- [ ] **Step 3: Delete the `WORKING_APP` export**

In `src/lib/default-app.ts`, delete the entire `WORKING_APP` block (the trailing export), i.e. remove:

```ts

/**
 * Placeholder shown on the device while the coding agent is generating an app.
 * Screen is small (240×135) so keep it to one short line.
 */
export const WORKING_APP = `
function init(ctx)
  screen.clear()
  screen.text(58, 58, "Working...", 2, 255, 255, 255)
  screen.flip()
end

function on_tick(ctx, dt_ms)
end
`
```

Leave `DEFAULT_APP` (and its doc comment) intact as the end of the file.

- [ ] **Step 4: Verify no stray references remain**

Run: `grep -rn "WORKING_APP" src/`
Expected: no matches.

- [ ] **Step 5: Typecheck, test, build**

Run: `npm run typecheck && npm test && npm run build`
Expected: typecheck clean; `Tests 15 passed (15)`; build succeeds.

- [ ] **Step 6: Commit**

```bash
git add src/agents/voice-agent.ts src/lib/default-app.ts
git commit -m "feat(m5stick-voice): device renders status from agent_status, drop WORKING_APP push"
```

End the commit body with:
```
Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
```

Stay on branch `spike-voice`. Do not branch or merge.

---

## Task 2: Device — render `agent_status` + tap-to-recover

**Files:**
- Modify: `examples/m5stick-voice/device/src/main.cpp` (includes near top; file-scope state after line 49; `setup()` ~line 171; `loop()` after line 192)

Run device commands from `examples/m5stick-voice/device`.

> **HARDWARE GATE:** Do all edits and the compile checks (Steps 1–5), then **STOP at Step 6 and hand back to the human (Matt) for an on-board test**. Do **not** run the commit step (Step 7) until Matt confirms it works on a real M5Stick. This is repo policy for `device/` changes.

- [ ] **Step 1: Add the includes for JSON + string helpers**

In `src/main.cpp`, the existing includes are:

```cpp
#include <M5Unified.h>
#include <Resident.h>
#include "DisplayDriver.h"
#include "IMUDriver.h"
#include "BuzzerDriver.h"
#include "PushButtonsDriver.h"
```

Add `<ArduinoJson.h>` and `<cstring>` immediately after `#include <Resident.h>`:

```cpp
#include <M5Unified.h>
#include <Resident.h>
#include <ArduinoJson.h>
#include <cstring>
#include "DisplayDriver.h"
#include "IMUDriver.h"
#include "BuzzerDriver.h"
#include "PushButtonsDriver.h"
```

(`JsonDocument` and the `doc["x"] | default` operator come from ArduinoJson; `strcmp` from `<cstring>`. `snprintf` is already available via the Arduino core.)

- [ ] **Step 2: Add file-scope status state**

In `src/main.cpp`, find:

```cpp
static volatile bool streaming = false;
```

Add directly below it:

```cpp

// ---- Coding-agent status (agent_status frames from the server) -------------
// errorActive gates "tap to dismiss the error and restore the suspended app".
// lastPressCount detects taps: only a short press bumps getTotalPressCount();
// a push-to-talk hold fires onHold() instead, so the two never collide.
static bool errorActive = false;
static uint16_t lastPressCount = 0;
```

- [ ] **Step 3: Register the `onMessage` handler in `setup()`**

In `setup()`, find the push-to-talk registration:

```cpp
    // Push-to-talk on button 0 (the front button). Uses the 200ms default
    // threshold so a tap is rejected but talk starts promptly.
    buttonDriver.setLongPress(0, onHold);
```

Insert this block immediately **before** it:

```cpp
    // Coding-agent status from the server. Render generic status text through
    // the status display, suspending a running app on the first "working" frame
    // so displayText() isn't gated off — the same takeover push-to-talk uses.
    sandbox.onMessage([](const char* /*transport*/, const char* type, JsonDocument& doc) {
        if (strcmp(type, "agent_status") != 0) return;
        const char* state = doc["state"] | "";

        if (strcmp(state, "working") == 0) {
            if (sandbox.isAppRunning() && !sandbox.isAppSuspended()) sandbox.suspendApp();
            errorActive = false;
            int lines = doc["lines"] | 0;
            if (lines > 0) {
                char buf[40];
                snprintf(buf, sizeof(buf), "Working...\n%d lines", lines);
                displayDriver.displayText(buf);
            } else {
                displayDriver.displayText("Working...");
            }
        } else if (strcmp(state, "validating") == 0) {
            displayDriver.displayText("Validating...");
        } else if (strcmp(state, "done") == 0) {
            bool success = doc["success"] | false;
            if (!success) {
                const char* msg = doc["message"] | "Error";
                displayDriver.displayText(msg && msg[0] ? msg : "Error");
                errorActive = true;
            }
            // success: do nothing — the finished app's {type:"app"} frame loads
            // next and loadApp() takes over the screen.
        }
        // "idle": ignored — avoids flashing the idle prompt before the app loads.
    });

```

- [ ] **Step 4: Add tap-to-recover in `loop()`**

In `loop()`, find:

```cpp
    M5.update();
    sandbox.loop();  // drives buttonDriver.update(), which fires onHold
```

Insert directly **after** the `sandbox.loop();` line:

```cpp

    // Tap (short press) dismisses a coding-agent error and restores the app
    // that was suspended to show status. Only taps bump pressCount (holds fire
    // onHold instead), so this never interferes with push-to-talk. ">" tolerates
    // the driver resetting pressCount to 0 on app load/unload (onAppReset).
    uint16_t pc = buttonDriver.getTotalPressCount();
    if (pc > lastPressCount && errorActive) {
        errorActive = false;
        if (sandbox.isAppRunning()) {
            sandbox.resumeApp();
            displayDriver.repaint();
        } else {
            showIdlePrompt();
        }
    }
    lastPressCount = pc;
```

- [ ] **Step 5: Compile both board environments**

Run: `pio run` (M5StickC Plus2)
Expected: `SUCCESS`.

Run: `pio run -e m5sticks3` (M5StickS3)
Expected: `SUCCESS`.

If either fails on `JsonDocument`/`strcmp`/`snprintf` being undeclared, confirm the Step 1 includes are present (`<ArduinoJson.h>`, `<cstring>`); `snprintf` may additionally need `<cstdio>` — add it alongside `<cstring>` if so.

- [ ] **Step 6: HARDWARE verification (required — pause here)**

Flash a device and have Matt confirm on the real M5Stick:

```bash
pio run -t upload            # M5StickC Plus2
# or: pio run -e m5sticks3 -t upload
```

Confirm, with the deployed server (redeploy the Task-1 server change first):
- Speaking "make a bouncing ball on the device" shows **"Working…"**, then **"Working… N lines"** with the count climbing, then **"Validating…"**, then the finished app runs.
- Inducing a failure (or simulating a `done` with `success:false`) shows the error text, and a **tap** on the front button restores the previously running app (or the idle prompt if none).
- Push-to-talk still works (hold → "Listening" → transcribes), and a tap during normal operation does nothing.

**Do not proceed to Step 7 until Matt confirms.** If something is off on hardware, return to the relevant step.

- [ ] **Step 7: Commit (only after Matt's hardware confirmation)**

```bash
git add examples/m5stick-voice/device/src/main.cpp
git commit -m "feat(m5stick-voice): device displays coding status from agent_status frames"
```

End the commit body with:
```
Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
```

Stay on branch `spike-voice`. Do not branch or merge.

---

## Self-review notes

- **Spec coverage:** server `WORKING_APP` push + constant removal (Task 1, Steps 1–3); device `onMessage` mapping `working`/`validating`/error→`displayText` with suspend-on-first-`working` (Task 2, Step 3); ignore `idle`/`done`-success (Step 3 comments); tap-to-recover via `getTotalPressCount()` (Step 4); malformed-field defaults via ArduinoJson `| default` (Step 3); hardware-verify-before-commit (Task 2 gate). Out-of-scope items (reasoning phase, overlay, animations) are not added.
- **Type/name consistency:** `errorActive` / `lastPressCount` declared (Task 2 Step 2) and used (Steps 3–4); `sandbox.onMessage`, `isAppRunning`, `isAppSuspended`, `suspendApp`, `resumeApp`, `getTotalPressCount`, `displayDriver.displayText`, `displayDriver.repaint`, `showIdlePrompt` all match the existing API surface in `main.cpp`/`ResidentSandbox.h`/`PushButtonsDriver.h`. `DEFAULT_APP` retained; `WORKING_APP` fully removed.
- **Placeholders:** none — every code step is complete.
```
