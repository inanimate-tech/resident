# m5stick-voice remote control — push the sim's app to the device

**Date:** 2026-06-03
**Status:** Approved (design); implementation pending
**Scope:** `examples/m5stick-voice` (device + server) + a new `Resident::Sandbox` app-suspend primitive in the library, version bump to `0.5.1-dev`.

## Goal

From the m5stick-voice web viewer, the user holds the device button and says
**"ok push app"**. The server then pushes the Lua app currently shown in the
**simulator** to the **physical device** over its existing WebSocket. The
physical device keeps its push-to-talk behaviour and **suspends** any running
app while in "Listening" mode, resuming it on button release.

## Background / current state

- **Device already receives apps.** `Resident::Sandbox` routes an incoming
  `{type:"app", code}` frame to `loadApp()` automatically
  (`src/ResidentSandbox.cpp:294`). The m5stick-voice server simply never
  forwards apps to the *device* connection today — only to browser monitors.
- **"The app in the sim" is server-authoritative.** `VoiceAgent.currentApp`
  (set by the `create_app` tool) is broadcast to monitors; the viewer renders
  `currentApp?.code ?? DEFAULT_APP` (`server/src/routes/devices.$deviceId.tsx:21`).
  There is no independent browser-side editor — the sim shows exactly that code.
- **Server tool pattern exists.** The realtime model already drives the app via
  function tools (`apply_css`, `create_app`) declared in `session.update`
  (`server/src/agents/voice-agent.ts`). `push_app` is a sibling tool.
- **The gap is app suspend.** `Sandbox::loop()` ticks the Lua app whenever
  `_appRunning` and connected (`src/ResidentSandbox.cpp:409`), and
  `DisplayDriver::displayText()` is suppressed while an app runs. There is no
  primitive to pause the tick and free the screen for a "Listening" indicator.
  Neither Resident nor HawthornRoomDevice has this: Hawthorn's
  `suspendTransports()` pauses the *network* (Courier) for OTA, and its
  heavy-message *deferral* stashes inbound app pushes during recording — but
  nothing pauses a *running* app's tick.

## Design

### Component 1 — Resident library: app-suspend primitive

Files: `src/ResidentSandbox.h`, `src/ResidentSandbox.cpp`. Additive,
non-breaking. Naming follows the existing app-scoped convention (`loadApp`,
`sendAppEvent`, `isAppRunning`); bare `suspend()`/`resume()` is avoided because
it collides with `Courier::Client::suspend()` reachable via `sandbox.courier()`.

New public API:

```cpp
void suspendApp();            // if running & not suspended: _appSuspended=true;  notifyAppRunning(false)
void resumeApp();             // if running & suspended:     _appSuspended=false; notifyAppRunning(true)
bool isAppSuspended() const;  // pairs with isAppRunning()
```

Behaviour:

- New private member `bool _appSuspended = false;`.
- `loop()` tick gate (`ResidentSandbox.cpp:409`) changes from
  `if (!_appRunning) return;` to `if (!_appRunning || _appSuspended) return;`.
  Courier `loop()` and extension `update()` continue to run while suspended —
  audio streaming, button polling, and app *reception* are unaffected; only the
  Lua `on_tick`/event dispatch stops.
- `notifyAppRunning(false)` while suspended flips `DisplayDriver._appRunning`
  to false, so `displayText("Listening")` is no longer suppressed.
- `isAppRunning()` keeps returning `_appRunning` (an app stays *loaded* while
  suspended). Suspension is a separate axis queried via `isAppSuspended()`.
- `loadApp()` clears `_appSuspended` (a freshly pushed app starts running, never
  stuck suspended).
- Both methods are no-ops when no app is loaded (`!_appRunning`).

**No native unit test.** The `test/unit` native harness cannot construct a
`Resident::Sandbox` — it links Esp32Lua and exposes `src/` headers, but compiles
no `ResidentSandbox.cpp` and has no native stubs for Courier::Client / ezTime
`Timezone` / ArduinoJson. No existing test instantiates `Sandbox` (by design —
`test_smoke` notes real Sandbox tests don't exist yet). Building that harness is
a disproportionate yak-shave for a 3-method, one-flag change. Coverage for this
primitive is therefore the **build gate** (it compiles into every example via
`run-tests.py build`) plus **on-hardware verification** (the firmware-verify
rule already mandates a board test). A `Sandbox` native test harness is a
worthwhile separate effort, out of scope here.

### Component 2 — Shared DisplayDriver: repaint-on-resume

File: `examples/m5stick-demo/device/lib/drivers/src/DisplayDriver.{h,cpp}`
(shared by m5stick-demo and m5stick-voice via `symlink://`).

"Listening" clears the *physical* display, but the off-screen sprite retains the
app's last frame. Animating apps redraw within ~100 ms on resume; a *static* app
(clock, QR — draws only in `init()`) would stay blank. Add:

```cpp
void repaint();   // if (_initialized) _canvas.pushSprite(0, 0);
```

The device calls it on resume to restore the last frame instantly, regardless of
whether the app redraws in `on_tick`. (Optional: if we choose not to touch the
shared driver, static apps stay blank until their next self-redraw — accepted
limitation. Design includes `repaint()`.)

### Component 3 — Device example: wire push-to-talk to suspend/resume

File: `examples/m5stick-voice/device/src/main.cpp`, in `onHold()`:

- `started == true`: `if (sandbox.isAppRunning()) sandbox.suspendApp();` then
  `displayDriver.displayText("Listening")` (existing line).
- `started == false`:
  `if (sandbox.isAppRunning()) { sandbox.resumeApp(); displayDriver.repaint(); }`
  `else { showIdlePrompt(); }`

When no app is loaded the existing idle-prompt behaviour is unchanged; the
deviceId line added earlier still shows.

### Component 4 — Server: `push_app` realtime tool

File: `server/src/agents/voice-agent.ts`.

- Add a no-arg `push_app` function tool to the `session.update` `tools` array,
  alongside `apply_css`/`create_app`. Description: "Push the app currently shown
  in the simulator to the physical device. Use when the user says to push / send
  / deploy / load the app onto the device / stick / hardware."
- Extend `SYSTEM_PROMPT` with a third numbered item describing `push_app`.
- `handleFunctionCall`: route `name === "push_app"` to `handlePushApp(callId)`.
- `handlePushApp(callId)`:
  - `const code = this.currentApp?.code ?? DEFAULT_APP` (import `DEFAULT_APP`
    from `../lib/default-app`) — **pushes exactly what the sim shows**, the
    default bouncing ball when no `create_app` has run.
  - `const frame = JSON.stringify({ type: "app", code })`.
  - `const devices = Array.from(this.getConnections("device"))`; `d.send(frame)`
    for each. The device's Courier WS parses it via `onCourierMessage` →
    `loadApp` (same shape the relay `/send` forwards).
  - Tool result: `{ ok: true, devices: devices.length }`, or
    `{ ok: false, error: "no device connected" }` when none.
  - Then `response.create` so the model speaks a brief confirmation.

### Component 5 — Version bump + changelog

- `library.json`: `"version": "0.5.0"` → `"0.5.1-dev"`.
- `idf_component.yml`: `version: "0.5.0"` → `"0.5.1-dev"`.
- `docs/changelog.md`: new section above `## v0.5.0`:

  ```
  ## v0.5.1-dev (<git-hash>)

  ### New features

  - `Resident::Sandbox::suspendApp()` / `resumeApp()` / `isAppSuspended()` —
    pause and resume a running app's tick without unloading it. While suspended,
    `loop()` skips the Lua `on_tick`/event dispatch (Courier and extension
    updates keep running) and the status display is freed for direct text. Used
    by the m5stick-voice example to show "Listening" over a running app during
    push-to-talk.
  ```

  `<git-hash>` filled with the short hash at commit time.

## Decisions (resolved)

- **No-app-yet:** push `DEFAULT_APP` (what the sim shows), not a refusal.
- **Resume:** in place — keep Lua state; do not re-run `init()`.
- **No deferral:** the realtime model calls `push_app` ~0.7 s after button
  release (post-commit), so the device is idle when the app lands — normal
  `loadApp`. Hawthorn-style inbound deferral is out of scope; if a push ever
  arrives mid-suspend, `loadApp` clears `_appSuspended` and the new app runs
  (may briefly draw over "Listening"). Accepted edge, documented not built.
- **repaint():** included, to keep static apps correct after a talk turn.

## Out of scope

- Authentication beyond the deviceId (same caveat as the rest of the relay).
- Browser-side app editing / choosing an app independent of the agent.
- Lifting Hawthorn's transport-suspend or message-deferral into Resident.

## Verification

- `./tools/run-tests.py unit` (new suspend/resume test) and `build`.
- Server: `npm test` + `npm run build` in `examples/m5stick-voice/server`.
- **On-hardware check before any commit** to `examples/*/src/`,
  `platformio.ini`, or the library (per the firmware-verify rule): flash the
  device, generate an app in the sim, say "ok push app", confirm it appears on
  the stick, then hold-to-talk and confirm the app suspends to "Listening" and
  resumes on release.
