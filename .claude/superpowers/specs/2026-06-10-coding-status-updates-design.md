# Coding status updates — design

**Date:** 2026-06-10
**Component:** `examples/m5stick-voice` (server `VoiceAgent` DO + browser monitor; firmware contract only)

## Goal

Give the coding job (`runCodingJob` in `server/src/agents/voice-agent.ts`) a
higher-resolution, live status stream, and broadcast it to **both** the browser
monitor connections and connected physical devices. Today the job reports only
`working` → (silence) → `done`, monitor-only, because codegen is a single
buffered fetch.

## Unified status model

Merge the old `agent_status` enum (`idle | working | done | error`) and the
proposed finer stream into **one** concept. One message type, broadcast to
monitors + devices:

```json
{ "type": "agent_status",
  "state": "idle | working | validating | done",
  "lines": 0,
  "success": true,
  "message": "" }
```

| state        | carries                                   | meaning |
|--------------|-------------------------------------------|---------|
| `idle`       | —                                         | resting; the state `snapshot` reports |
| `working`    | `lines: N`                                | generating Lua. "started" = `working` with `lines: 0`, then climbs |
| `validating` | —                                         | running `validateLuaCode` |
| `done`       | `success: bool`, `message` (err text when `success:false`) | terminal **event** — state then immediately returns to `idle` |

`done` is fire-once and never persisted. A tab refreshing after a job finishes
gets `idle` from `snapshot` — no stale toast. The old `error` state is folded
into `done` with `success:false`.

### Sequence (one retry)

```
working(0) → working(1..N) → validating → working(1..M) → validating → done(success) → idle
```

The client infers retry count from `validating → working` transitions; the
server does not send a distinct "retrying" signal.

## Server-side implementation (`voice-agent.ts`)

### Streaming codegen

`callCodegenChat` switches to `stream: true`:

- Read `resp.body` as an SSE stream: `getReader()` → `TextDecoder` → split on
  `\n\n` → parse `data:` lines, stop on `[DONE]`, accumulate
  `choices[0].delta.content`.
- New param `onProgress(lines: number)`. Count newlines in the accumulated text
  and call `onProgress` **time-throttled** (≥250 ms between emissions; always one
  final flush at stream end).
- Fence-stripping (`replace(/^```.../)`) applies to the fully-accumulated text,
  as today.
- Signature: `callCodegenChat(description, followups, signal, onProgress)`.

The throttle + newline-counting is extracted into a small standalone helper so
it is unit-testable independently of the DO.

### Broadcast

`setAgentStatus` becomes the broadcaster. New signature
`setAgentStatus(state, { lines?, success?, message? })`. It builds the
`agent_status` frame and sends it to **both** `getConnections("monitor")` and
`getConnections("device")`. `toMonitors` stays unchanged for monitor-only
traffic (transcript, css, app code, binary FFT frames).

### Emission points in `runCodingJob`

Refactor the attempt-1-then-conditional-attempt-2 block into a small
`for (attempt of [1, 2])` loop so per-attempt emissions aren't duplicated:

```
working({ lines: 0 })                         // at handleFunctionCall, as today
for attempt in [1, 2]:
  callCodegenChat(..., lines => working({ lines }))   // streamed progress
  validating({})
  validateLuaCode()
  if ok: break
done({ success, message? })                   // success=false carries the error
idle({})                                       // immediately after
```

`done` + `idle` fire from a reworked `finishJob`, which keeps its existing
`[system] completed/failed` injection back to the realtime model (a separate
channel, unaffected by this work).

### WORKING_APP placeholder is unchanged

`handleFunctionCall` still pushes the `WORKING_APP` Lua placeholder to physical
devices at job start (`voice-agent.ts:321`). Physical devices display the
running app and the status stream as **separate layers** — the placeholder
gives them an app to show; `agent_status` rides alongside it.

## Client (`server/src/`)

### `useVoiceMonitor` / types

- `AgentStatus` type → `"idle" | "working" | "validating" | "done"`.
- The `agent_status` handler reads `state`, `lines`, `success`, `message`.
- New exposed state:
  - `workingLines: number`
  - `lastDone: { success: boolean; message?: string } | null` — rendered as a
    dismissable toast.
  - `retryCount: number` — incremented on each `validating → working` transition
    within a job; reset when a fresh job starts (`working`, lines 0).
- `snapshot` only ever carries `idle | working | validating` (never `done`).

### `StatusPill`

- Renders `working` as "Working… N lines", "Validating…" for `validating`,
  nothing for `idle`. Shows a retry hint when `retryCount > 0`.
- `done` is **not** the pill — a separate dismissable toast component shows
  success ("App ready") or the error `message`.

## Firmware contract (device side — owned separately)

The device receives, on its existing WebSocket, alongside `{type:"app",code}`:

```json
{ "type": "agent_status", "state": "working|validating|done|idle",
  "lines": 0, "success": true, "message": "" }
```

It renders this however it likes, independent of the running app. No firmware
changes are specified here beyond the contract.

## Error handling

- Streaming fetch failures (`!resp.ok`, network) throw inside `callCodegenChat`;
  `runCodingJob`'s catch path maps to `done({ success:false, message })` → `idle`.
- A second failed validation → `done({ success:false, message: validation.error })`.
- Aborted job (`signal.aborted`, superseded by a newer request) returns silently
  without emitting `done`, as today.

## Testing

- Unit-test the extracted throttle / newline-count helper (pure function).
- Update `lua-validator` tests only if its surface is touched (not expected).
- DO streaming, broadcast fan-out, and device rendering are verified on
  hardware before committing (firmware changes are never committed on a
  compile-pass alone).

## Out of scope

- `lastLine` field (dropped — `lines: N` is enough; YAGNI).
- Any firmware rendering implementation.
- Changing the realtime-model tool surface or the `apply_css` / `push_app` paths.
