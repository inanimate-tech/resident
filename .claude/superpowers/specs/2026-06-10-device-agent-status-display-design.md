# Device agent-status display — design

**Date:** 2026-06-10
**Component:** `examples/m5stick-voice` — server `VoiceAgent` (one deletion) + device firmware `device/src/main.cpp`

## Goal

Make the physical M5Stick render coding-job status directly from the
`agent_status` WebSocket frames, replacing the current `WORKING_APP` Lua
placeholder that the server pushes. Keep the display generic and
straightforward — plain status text via the device's existing `displayText`
path, carrying the live line count.

## Background

The server already broadcasts `agent_status` frames to device connections
(`broadcastAgentStatus` in `voice-agent.ts`):

```json
{ "type": "agent_status", "state": "idle|working|validating|done",
  "lines": 0, "success": true, "message": "" }
```

Today the device shows "Working…" only because the server *also* pushes a
`WORKING_APP` Lua app (`pushAppToDevices(WORKING_APP)` in `handleFunctionCall`),
which the device runs like any app. That push is the trigger we are replacing.

The Resident library routes unknown frame `type`s to a user callback
(`sandbox.onMessage(...)`, `src/ResidentSandbox.cpp:323`); `agent_status` is
unknown to the library, so the firmware can handle it with no library change.

## Server change (`server/src/agents/voice-agent.ts`)

In `handleFunctionCall`, the job already emits
`setAgentStatus("working", { lines: 0 })` (broadcast to devices). Remove the
placeholder push that follows it:

```ts
// delete:
const working = this.pushAppToDevices(WORKING_APP)
if (working > 0) console.log("[voice] pushed Working... ->", working, "device(s)")
```

`WORKING_APP` is then unused (verified: its only reference is this push;
`DEFAULT_APP` is still used by `handlePushApp` and the route). So:

- remove `WORKING_APP` from the import in `voice-agent.ts`, and
- delete the `WORKING_APP` export from `server/src/lib/default-app.ts`.

No other server change — devices already receive the full `agent_status` stream.

## Device change (`device/src/main.cpp`)

### Display surface

Status renders through the existing `displayDriver.displayText(const char*)` —
the same path used for "Listening", "Connecting…", and the idle prompt.
`displayText` is gated off while a Lua app is running (returns early when
`_appRunning`), so to draw over a running app we suspend it first, exactly as
the existing `onHold` push-to-talk does
(`if (sandbox.isAppRunning()) sandbox.suspendApp();`).

### State

File-scope additions:

```cpp
static bool errorActive = false;        // showing an error, awaiting a tap
static uint16_t lastPressCount = 0;     // for tap detection (see below)
```

### `onMessage` handler (registered in `setup()`)

```cpp
sandbox.onMessage([](const char* /*transport*/, const char* type, JsonDocument& doc) {
  if (strcmp(type, "agent_status") != 0) return;
  const char* state = doc["state"] | "";

  if (strcmp(state, "working") == 0) {
    if (sandbox.isAppRunning() && !sandbox.isAppSuspended()) sandbox.suspendApp();
    errorActive = false;
    int lines = doc["lines"] | 0;
    if (lines > 0) {
      char buf[40];
      snprintf(buf, sizeof buf, "Working...\n%d lines", lines);
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
    // success: do nothing — the finished app's {type:"app"} frame loads next
    //          and loadApp() takes over the screen.
  }
  // "idle": ignored — avoids flashing the idle prompt before the app loads.
});
```

Notes:
- On the first `working` frame an actively-running app is suspended; subsequent
  `working` frames (line count climbing) find it already suspended and just
  redraw the text. `displayText` clears and reprints, so the number updates in
  place.
- The handler does not unload anything; the previous app stays loaded-but-
  suspended, so it can be restored on error (below) or replaced by `loadApp`
  when the finished app arrives.

### Tap-to-recover on error

The button driver increments `getTotalPressCount()` **only on a tap** (a short
press); a long-press fires `onHold(false)` on release and never bumps the count
(`PushButtonsDriver::update`). So taps are cleanly distinguishable from
push-to-talk holds without modifying the shared driver.

In `loop()` (after `sandbox.loop()`):

```cpp
uint16_t pc = buttonDriver.getTotalPressCount();
if (pc > lastPressCount && errorActive) {
  errorActive = false;
  if (sandbox.isAppRunning()) {
    sandbox.resumeApp();
    displayDriver.repaint();   // restore the suspended app's last frame
  } else {
    showIdlePrompt();
  }
}
lastPressCount = pc;
```

`pc > lastPressCount` (rather than `!=`) tolerates the driver resetting
`pressCount` to 0 on `onAppReset` (app load/unload). A tap while no error is
showing does nothing — identical to current behavior. A hold still works as
today (talk again); on release `onHold(false)` already resumes the suspended app
or shows the idle prompt.

## End-to-end behavior

Device-only flow (no browser monitor), one job:

1. User holds button, talks → `onHold(true)` suspends any app, shows "Listening".
2. Release → `onHold(false)` resumes the app (or idle prompt).
3. Server transcribes → realtime model calls `create_app` → server broadcasts
   `agent_status working{lines:0}` → device suspends the app, shows "Working...".
4. Codegen streams → `working{lines:N}` frames → device shows "Working… N lines".
5. `validating` → "Validating…".
6a. **Success:** `done{success:true}` (device ignores) → `idle` (ignored) →
    server pushes `{type:"app", code}` → `loadApp` replaces the screen, app runs.
6b. **Error:** `done{success:false, message}` → device shows the message; a tap
    resumes the previously-suspended app (or idle prompt).

## Error handling

- Malformed / missing `agent_status` fields: `doc["..."] | default` (ArduinoJson)
  yields safe defaults, so a missing `lines`/`success`/`message` degrades to
  "Working...", non-success false, "Error" respectively.
- Frames arriving while push-to-talk is held are not expected (the job runs
  after release); no special guard is added. (If observed on hardware to clobber
  "Listening", add a `streaming` guard — deferred unless seen.)

## Testing

- Server: `npm run typecheck` / `npm test` / `npm run build` stay green after
  removing the push and the constant.
- Device: builds for both envs (`pio run` and `pio run -e m5sticks3`).
- **Hardware verification is required before committing the `device/` change**
  (repo policy): confirm on a real M5Stick that a spoken "make a … on the
  device" shows "Working… N lines" climbing, then "Validating…", then the
  finished app runs; and that an induced error shows the message and a tap
  restores the prior app.

## Out of scope

- Any "thinking/reasoning" phase or `reasoning_effort` change (separate, later).
- Animations, status-bar overlay, or compositing over a running app (the chosen
  approach is a generic full-screen takeover via `displayText`).
- Browser/monitor behavior (unchanged).
