# Coding Status Updates Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stream high-resolution coding-job status (`working` with live line count → `validating` → `done`) from the `VoiceAgent` Durable Object to both browser monitors and connected physical devices.

**Architecture:** Switch the codegen call from a buffered fetch to a streamed SSE read, counting lines as Lua arrives and emitting throttled `working` updates. Merge the old `agent_status` enum into one model (`idle | working | validating | done`), broadcast every status to monitor **and** device connections, and surface `done` as a transient toast on the client.

**Tech Stack:** TypeScript, Cloudflare Workers / Durable Objects (`agents` SDK), React 19, TanStack Start/Router, Vitest. All work is under `examples/m5stick-voice/server/`.

---

## Working directory

All paths below are relative to `examples/m5stick-voice/server/`. Run all commands from there:

```bash
cd examples/m5stick-voice/server
```

## File structure

- **Create** `src/lib/codegen-stream.ts` — pure helpers for codegen streaming: SSE line parsing, line counting, and the time-throttled progress emitter. One responsibility: turn a stream of SSE text into throttled line-count callbacks. Unit-tested.
- **Create** `src/lib/codegen-stream.test.ts` — Vitest tests for the above.
- **Create** `src/components/DoneToast.tsx` — dismissable toast showing job success/error.
- **Modify** `src/agents/voice-agent.ts` — streaming `callCodegenChat`, broadcast `setAgentStatus`, reworked `runCodingJob` + `finishJob`, new `AgentStatus` type, snapshot shape.
- **Modify** `src/hooks/useVoiceMonitor.ts` — consume the merged `agent_status`, expose `workingLines` / `retryCount` / `lastDone` / `dismissDone`.
- **Modify** `src/components/StatusPill.tsx` — new enum, render line count + retry.
- **Modify** `src/components/Header.tsx` — pass line count / retry to the pill, drop the inline done/error message block (replaced by the toast).
- **Modify** `src/routes/devices.$deviceId.tsx` — wire the new hook fields and render `DoneToast`.

The firmware that consumes `{type:"agent_status",...}` on the device side is owned separately and is **not** part of this plan — only the wire contract is.

---

## Task 1: SSE line parser

**Files:**
- Create: `src/lib/codegen-stream.ts`
- Test: `src/lib/codegen-stream.test.ts`

- [ ] **Step 1: Write the failing test**

Create `src/lib/codegen-stream.test.ts`:

```ts
import { describe, expect, it } from "vitest"
import { parseSSELine } from "./codegen-stream"

describe("parseSSELine", () => {
  it("extracts delta content from a data line", () => {
    const line = `data: ${JSON.stringify({ choices: [{ delta: { content: "hi" } }] })}`
    expect(parseSSELine(line)).toEqual({ content: "hi" })
  })

  it("flags the [DONE] sentinel", () => {
    expect(parseSSELine("data: [DONE]")).toEqual({ done: true })
  })

  it("ignores blank lines and non-data lines", () => {
    expect(parseSSELine("")).toEqual({})
    expect(parseSSELine(": keep-alive")).toEqual({})
  })

  it("ignores data lines with no content delta (e.g. role-only chunk)", () => {
    const line = `data: ${JSON.stringify({ choices: [{ delta: { role: "assistant" } }] })}`
    expect(parseSSELine(line)).toEqual({})
  })

  it("returns empty on malformed JSON rather than throwing", () => {
    expect(parseSSELine("data: {not json")).toEqual({})
  })
})
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npm test -- codegen-stream`
Expected: FAIL — `parseSSELine` is not exported / module not found.

- [ ] **Step 3: Write minimal implementation**

Create `src/lib/codegen-stream.ts`:

```ts
/** Parse one line of an OpenAI chat-completions SSE stream. */
export function parseSSELine(line: string): { content?: string; done?: boolean } {
  const trimmed = line.trim()
  if (!trimmed.startsWith("data:")) return {}
  const payload = trimmed.slice(5).trim()
  if (payload === "[DONE]") return { done: true }
  try {
    const json = JSON.parse(payload) as {
      choices?: Array<{ delta?: { content?: unknown } }>
    }
    const content = json.choices?.[0]?.delta?.content
    return typeof content === "string" ? { content } : {}
  } catch {
    return {}
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `npm test -- codegen-stream`
Expected: PASS (5 tests).

- [ ] **Step 5: Commit**

```bash
git add src/lib/codegen-stream.ts src/lib/codegen-stream.test.ts
git commit -m "feat(m5stick-voice): SSE line parser for streamed codegen"
```

---

## Task 2: Line-counting throttled progress emitter

**Files:**
- Modify: `src/lib/codegen-stream.ts`
- Test: `src/lib/codegen-stream.test.ts`

- [ ] **Step 1: Write the failing test**

Append to `src/lib/codegen-stream.test.ts`:

```ts
import { countLines, createLineProgress } from "./codegen-stream"

describe("countLines", () => {
  it("counts completed (newline-terminated) lines", () => {
    expect(countLines("")).toBe(0)
    expect(countLines("one line, no newline")).toBe(0)
    expect(countLines("a\nb\n")).toBe(2)
    expect(countLines("a\nb")).toBe(1)
  })
})

describe("createLineProgress", () => {
  it("emits only when the line count changes and the interval has elapsed", () => {
    let clock = 0
    const emitted: number[] = []
    const p = createLineProgress((n) => emitted.push(n), { intervalMs: 250, now: () => clock })

    p.update("a\n")          // t=0: first change, 0>=250? no -> suppressed
    clock = 100
    p.update("a\nb\n")       // t=100: still < 250 -> suppressed
    clock = 300
    p.update("a\nb\nc\n")    // t=300: >=250 and changed -> emit 3
    clock = 350
    p.update("a\nb\nc\n")    // no change -> nothing
    expect(emitted).toEqual([3])
  })

  it("flush emits the final count if it was never emitted", () => {
    let clock = 0
    const emitted: number[] = []
    const p = createLineProgress((n) => emitted.push(n), { intervalMs: 250, now: () => clock })
    p.update("a\nb\n")       // suppressed (t=0)
    p.flush()                // emits 2
    expect(emitted).toEqual([2])
  })

  it("flush is a no-op when the latest count was already emitted", () => {
    let clock = 1000
    const emitted: number[] = []
    const p = createLineProgress((n) => emitted.push(n), { intervalMs: 250, now: () => clock })
    p.update("a\nb\n")       // t=1000, elapsed since 0 -> emit 2
    p.flush()                // already emitted 2 -> no-op
    expect(emitted).toEqual([2])
  })
})
```

> Note: the first `update` at `t=0` is suppressed because `0 - 0 >= 250` is false; the job separately emits `working({lines:0})` at start (Task 5), so the initial state is still shown.

- [ ] **Step 2: Run test to verify it fails**

Run: `npm test -- codegen-stream`
Expected: FAIL — `countLines` / `createLineProgress` not exported.

- [ ] **Step 3: Write minimal implementation**

Append to `src/lib/codegen-stream.ts`:

```ts
/** Number of completed (newline-terminated) lines in `text`. */
export function countLines(text: string): number {
  let n = 0
  for (let i = 0; i < text.length; i++) if (text[i] === "\n") n++
  return n
}

export interface LineProgress {
  /** Feed the full accumulated text so far. */
  update(accumulated: string): void
  /** Emit the final count if it has not been emitted yet. */
  flush(): void
}

/**
 * Emits the completed-line count of streamed text, throttled to at most one
 * emission per `intervalMs`, only when the count changes. `now` is injectable
 * for tests; defaults to `Date.now`.
 */
export function createLineProgress(
  emit: (lines: number) => void,
  opts: { intervalMs?: number; now?: () => number } = {},
): LineProgress {
  const intervalMs = opts.intervalMs ?? 250
  const now = opts.now ?? Date.now
  let lastEmitAt = 0
  let emittedLines = -1
  let latestLines = 0
  return {
    update(accumulated) {
      latestLines = countLines(accumulated)
      const t = now()
      if (latestLines !== emittedLines && t - lastEmitAt >= intervalMs) {
        emittedLines = latestLines
        lastEmitAt = t
        emit(latestLines)
      }
    },
    flush() {
      if (latestLines !== emittedLines) {
        emittedLines = latestLines
        emit(latestLines)
      }
    },
  }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `npm test -- codegen-stream`
Expected: PASS (all `codegen-stream` tests, including Task 1's).

- [ ] **Step 5: Commit**

```bash
git add src/lib/codegen-stream.ts src/lib/codegen-stream.test.ts
git commit -m "feat(m5stick-voice): throttled line-count progress emitter"
```

---

## Task 3: Stream the codegen call

**Files:**
- Modify: `src/agents/voice-agent.ts` (imports; `callCodegenChat` at `voice-agent.ts:429-461`)

This task has no unit test (it drives a network fetch inside the DO); it is covered by `npm run typecheck` here and the hardware verification in Task 10.

- [ ] **Step 1: Add the import**

At the top of `src/agents/voice-agent.ts`, after the existing `validateLuaCode` import, add:

```ts
import { parseSSELine, createLineProgress } from "../lib/codegen-stream"
```

- [ ] **Step 2: Replace `callCodegenChat` with a streaming version**

Replace the entire `callCodegenChat` method with:

```ts
  private async callCodegenChat(
    description: string,
    followups: { role: "assistant" | "user"; content: string }[],
    signal: AbortSignal,
    onProgress: (lines: number) => void,
  ): Promise<string> {
    const key = this.env.OPENAI_API_KEY
    if (!key) throw new Error("OPENAI_API_KEY not set")

    const messages = [
      { role: "system", content: CODEGEN_SYSTEM },
      { role: "user", content: description },
      ...followups,
    ]

    const resp = await fetch("https://api.openai.com/v1/chat/completions", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${key}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ model: CODEGEN_MODEL, messages, stream: true }),
      signal,
    })
    if (!resp.ok) {
      const body = await resp.text().catch(() => "")
      throw new Error(`OpenAI ${resp.status}: ${body.slice(0, 400)}`)
    }
    if (!resp.body) throw new Error("OpenAI returned no response stream")

    const progress = createLineProgress(onProgress)
    const reader = resp.body.getReader()
    const decoder = new TextDecoder()
    let buffer = ""
    let content = ""

    try {
      for (;;) {
        const { done, value } = await reader.read()
        if (done) break
        buffer += decoder.decode(value, { stream: true })
        let nl: number
        while ((nl = buffer.indexOf("\n")) !== -1) {
          const line = buffer.slice(0, nl)
          buffer = buffer.slice(nl + 1)
          const parsed = parseSSELine(line)
          if (parsed.done) { buffer = ""; break }
          if (parsed.content) {
            content += parsed.content
            progress.update(content)
          }
        }
      }
    } finally {
      reader.releaseLock()
    }
    progress.flush()

    // The line count above includes any markdown fence lines; the displayed app
    // strips them here. Progress is an indicator, so the small discrepancy is fine.
    return content.replace(/^```(?:lua)?\s*/i, "").replace(/```\s*$/i, "").trim()
  }
```

- [ ] **Step 3: Verify it still type-checks (callers updated in Task 5)**

Run: `npm run typecheck`
Expected: ONE error only — `runCodingJob` calls `callCodegenChat` without the new `onProgress` argument. That is fixed in Task 5. Do not commit yet; continue to Task 4.

> If you prefer a green checkpoint, you may do Tasks 4 and 5 before running `typecheck`/committing. Tasks 3–5 form one logically atomic server change.

---

## Task 4: Broadcast status to monitors and devices

**Files:**
- Modify: `src/agents/voice-agent.ts` (`AgentStatus` type `:51`; instance fields `:69-71`; `onConnect` snapshot `:82-90`; `setAgentStatus` `:483-487`)

- [ ] **Step 1: Widen the `AgentStatus` type**

Replace:

```ts
type AgentStatus = "idle" | "working" | "done" | "error"
```

with:

```ts
type AgentStatus = "idle" | "working" | "validating" | "done"
```

- [ ] **Step 2: Add a persisted line-count field**

In the class, find:

```ts
  // M2 codegen state.
  private agentStatus: AgentStatus = "idle"
  private agentMessage?: string
```

and replace with:

```ts
  // M2 codegen state.
  private agentStatus: AgentStatus = "idle"
  private agentLines = 0
```

(`agentMessage` is removed — error text now travels only with the transient
`done` event, which is never persisted.)

- [ ] **Step 3: Update the snapshot payload**

In `onConnect`, replace the snapshot send:

```ts
      connection.send(JSON.stringify({
        type: "snapshot",
        agent_status: this.agentStatus,
        message: this.agentMessage,
        app: this.currentApp,
        css: this.currentCss,
      }))
```

with:

```ts
      connection.send(JSON.stringify({
        type: "snapshot",
        agent_status: this.agentStatus, // idle | working | validating (never done)
        lines: this.agentLines,
        app: this.currentApp,
        css: this.currentCss,
      }))
```

- [ ] **Step 4: Replace `setAgentStatus` with the broadcaster**

Replace the entire `setAgentStatus` method:

```ts
  private setAgentStatus(state: AgentStatus, message: string | undefined): void {
    this.agentStatus = state
    this.agentMessage = message
    this.toMonitors({ type: "agent_status", state, message })
  }
```

with:

```ts
  private setAgentStatus(
    state: AgentStatus,
    extra: { lines?: number; success?: boolean; message?: string } = {},
  ): void {
    // `done` is a transient event (the toast); the caller follows it with `idle`.
    // Only persist resting states so a refreshed tab's snapshot is accurate.
    if (state !== "done") {
      this.agentStatus = state
      this.agentLines = extra.lines ?? 0
    }
    this.broadcastAgentStatus({ type: "agent_status", state, ...extra })
  }

  /** Send a JSON status frame to every monitor AND every device connection.
   *  Named to avoid colliding with the base class's `broadcastStatus()`. */
  private broadcastAgentStatus(obj: unknown): void {
    const s = JSON.stringify(obj)
    for (const m of this.getConnections("monitor")) m.send(s)
    for (const d of this.getConnections("device")) d.send(s)
  }
```

(Type-check is deferred to Task 5, where the remaining callers are updated.)

---

## Task 5: Rework the job lifecycle emissions

**Files:**
- Modify: `src/agents/voice-agent.ts` (`handleFunctionCall` `:317`; `runCodingJob` `:379-427`; `finishJob` `:463-481`)

- [ ] **Step 1: Update the "started" emission in `handleFunctionCall`**

Find:

```ts
    // Tell the viewer.
    this.setAgentStatus("working", undefined)
```

Replace with:

```ts
    // Tell the viewer + device: started (zero lines so far).
    this.setAgentStatus("working", { lines: 0 })
```

- [ ] **Step 2: Rewrite `runCodingJob` as an attempt loop**

Replace the entire `runCodingJob` method with:

```ts
  private async runCodingJob(
    jobId: string,
    description: string,
    signal: AbortSignal,
  ): Promise<void> {
    try {
      const followups: { role: "assistant" | "user"; content: string }[] = []
      let code = ""
      let validation: ValidationResult = { ok: false }

      for (let attempt = 1; attempt <= 2; attempt++) {
        code = await this.callCodegenChat(
          description,
          followups,
          signal,
          (lines) => this.setAgentStatus("working", { lines }),
        )
        if (signal.aborted) return

        this.setAgentStatus("validating", {})
        validation = await validateLuaCode(code)
        if (signal.aborted) return
        if (validation.ok) break

        console.warn(`[voice] codegen v${attempt} validation failed:`, validation.error)
        followups.push(
          { role: "assistant", content: code },
          {
            role: "user",
            content: `That code failed validation with: ${validation.error}. Fix it. Return only Lua, no commentary.`,
          },
        )
      }

      if (!validation.ok) {
        this.finishJob(jobId, false, validation.error ?? "validation failed")
        return
      }

      this.appVersion += 1
      this.currentApp = { code, version: this.appVersion }

      // With a monitor present, push into the simulator (the user pushes to
      // hardware separately via push_app). With no monitor, send straight to
      // any connected physical device.
      const monitors = Array.from(this.getConnections("monitor")).length
      if (monitors > 0) {
        this.toMonitors({ type: "app", code, version: this.appVersion })
      } else {
        const devices = this.pushAppToDevices(code)
        console.log("[voice] no monitor — pushed app ->", devices, "device(s)")
      }
      this.finishJob(jobId, true, undefined)
    } catch (err) {
      if (signal.aborted) return
      const msg = err instanceof Error ? err.message : String(err)
      this.finishJob(jobId, false, msg)
    }
  }
```

- [ ] **Step 3: Import the `ValidationResult` type**

Update the validator import so the type used above is in scope. Change:

```ts
import { validateLuaCode } from "../lib/lua-validator"
```

to:

```ts
import { validateLuaCode, type ValidationResult } from "../lib/lua-validator"
```

- [ ] **Step 4: Rewrite `finishJob` to emit `done` then `idle`**

Replace the entire `finishJob` method:

```ts
  private finishJob(jobId: string, state: "done" | "error", message: string | undefined): void {
    this.setAgentStatus(state, message)
    if (this.openai && this.openaiReady) {
      const content = state === "done"
        ? `[create_app jobId=${jobId}] completed successfully`
        : `[create_app jobId=${jobId}] failed: ${message ?? "unknown error"}`
      this.openai.send(JSON.stringify({
        type: "conversation.item.create",
        item: {
          type: "message",
          role: "user",
          content: [{ type: "input_text", text: `[system] ${content}` }],
        },
      }))
    }
  }
```

with:

```ts
  private finishJob(jobId: string, success: boolean, message: string | undefined): void {
    // Transient terminal event (drives the client toast), then back to idle.
    this.setAgentStatus("done", { success, message })
    this.setAgentStatus("idle", {})

    if (this.openai && this.openaiReady) {
      const content = success
        ? `[create_app jobId=${jobId}] completed successfully`
        : `[create_app jobId=${jobId}] failed: ${message ?? "unknown error"}`
      this.openai.send(JSON.stringify({
        type: "conversation.item.create",
        item: {
          type: "message",
          role: "user",
          content: [{ type: "input_text", text: `[system] ${content}` }],
        },
      }))
    }
  }
```

- [ ] **Step 5: Type-check the whole server change (Tasks 3–5)**

Run: `npm run typecheck`
Expected: PASS (no errors).

- [ ] **Step 6: Run the unit tests**

Run: `npm test`
Expected: PASS — `codegen-stream` and `lua-validator` suites all green.

- [ ] **Step 7: Commit**

```bash
git add src/agents/voice-agent.ts
git commit -m "feat(m5stick-voice): stream codegen status to monitors and devices"
```

---

## Task 6: StatusPill — new enum, line count, retry

**Files:**
- Modify: `src/components/StatusPill.tsx`

- [ ] **Step 1: Replace the component**

Replace the entire contents of `src/components/StatusPill.tsx` with:

```tsx
export type AgentStatus = "idle" | "working" | "validating" | "done"

interface Props {
  status: AgentStatus
  lines?: number
  retryCount?: number
}

/** Coarse status pill. `done` is shown as a toast, not here, but kept in the
 *  style map for type completeness. */
export function StatusPill({ status, lines = 0, retryCount = 0 }: Props) {
  const styles: Record<AgentStatus, { dot: string; label: string }> = {
    idle:       { dot: "#666",    label: "idle" },
    working:    { dot: "#e0c542", label: lines > 0 ? `working · ${lines} lines` : "working" },
    validating: { dot: "#54a0e0", label: "validating" },
    done:       { dot: "#3fd07d", label: "done" },
  }
  const s = styles[status]
  const active = status === "working" || status === "validating"
  const label = retryCount > 0 ? `${s.label} · retry ${retryCount}` : s.label
  return (
    <span style={{
      display: "inline-flex", alignItems: "center", gap: 6,
      fontSize: 12, opacity: 0.8,
    }}>
      <span style={{
        width: 8, height: 8, borderRadius: 4, background: s.dot,
        animation: active ? "pulse 1s ease-in-out infinite" : undefined,
      }} />
      <span>{label}</span>
      <style>{`@keyframes pulse { 50% { opacity: 0.4 } }`}</style>
    </span>
  )
}
```

- [ ] **Step 2: Commit**

```bash
git add src/components/StatusPill.tsx
git commit -m "feat(m5stick-voice): status pill shows line count, validating, retry"
```

> The repo has no React component test harness; correctness here is covered by `npm run typecheck` (Task 9) and the hardware/browser check (Task 10).

---

## Task 7: DoneToast component

**Files:**
- Create: `src/components/DoneToast.tsx`

- [ ] **Step 1: Create the component**

Create `src/components/DoneToast.tsx`:

```tsx
interface Props {
  success: boolean
  message?: string
  onDismiss: () => void
}

/** Dismissable bottom-centre toast shown when a coding job finishes. */
export function DoneToast({ success, message, onDismiss }: Props) {
  return (
    <div style={{
      position: "fixed", left: "50%", bottom: 24, transform: "translateX(-50%)",
      zIndex: 50, maxWidth: 420,
      padding: "10px 14px",
      display: "flex", alignItems: "flex-start", gap: 10,
      fontSize: 13,
      fontFamily: "ui-monospace, SFMono-Regular, Menlo, monospace",
      background: success ? "rgba(63,208,125,.12)" : "rgba(224,83,63,.15)",
      color: success ? "#a8e6c1" : "#f3a298",
      border: `1px solid ${success ? "rgba(63,208,125,.4)" : "rgba(224,83,63,.4)"}`,
      borderRadius: 6,
      boxShadow: "0 4px 16px rgba(0,0,0,.4)",
      whiteSpace: "pre-wrap", wordBreak: "break-word",
    }}>
      <span style={{ flex: 1 }}>
        {success ? "App ready" : `error: ${message ?? "unknown error"}`}
      </span>
      <button onClick={onDismiss} aria-label="Dismiss" style={{
        background: "none", border: "none", color: "inherit",
        cursor: "pointer", fontSize: 14, lineHeight: 1, opacity: 0.7, padding: 0,
      }}>×</button>
    </div>
  )
}
```

- [ ] **Step 2: Commit**

```bash
git add src/components/DoneToast.tsx
git commit -m "feat(m5stick-voice): dismissable done/error toast component"
```

---

## Task 8: useVoiceMonitor — consume merged status

**Files:**
- Modify: `src/hooks/useVoiceMonitor.ts`

- [ ] **Step 1: Update the exported interface and imports**

At the top, the existing import already pulls `AgentStatus` from `../components/StatusPill` — keep it. Replace the `VoiceMonitor` interface with:

```ts
export interface DoneEvent {
  success: boolean
  message?: string
}

export interface VoiceMonitor {
  status: string
  transcript: TranscriptItem[]
  agentStatus: AgentStatus // idle | working | validating
  workingLines: number
  retryCount: number
  lastDone: DoneEvent | null
  dismissDone: () => void
  currentApp?: CurrentApp
  css: string
  setFrameHandler: (cb: ((buf: ArrayBuffer) => void) | null) => void
}
```

- [ ] **Step 2: Replace the status state hooks**

Find:

```ts
  const [agentStatus, setAgentStatus] = useState<AgentStatus>("idle")
  const [agentMessage, setAgentMessage] = useState<string | undefined>(undefined)
```

Replace with:

```ts
  const [agentStatus, setAgentStatus] = useState<AgentStatus>("idle")
  const [workingLines, setWorkingLines] = useState(0)
  const [retryCount, setRetryCount] = useState(0)
  const [lastDone, setLastDone] = useState<DoneEvent | null>(null)
  const prevStatusRef = useRef<AgentStatus>("idle")
```

- [ ] **Step 3: Add the `dismissDone` callback**

Just below `setFrameHandler` definition, add:

```ts
  const dismissDone: VoiceMonitor["dismissDone"] = () => setLastDone(null)
```

- [ ] **Step 4: Replace the `agent_status` case**

Replace:

```ts
        case "agent_status":
          if (isAgentStatus(m.state)) setAgentStatus(m.state)
          setAgentMessage(typeof m.message === "string" ? m.message : undefined)
          break
```

with:

```ts
        case "agent_status": {
          if (!isAgentStatus(m.state)) break
          const next = m.state
          const prev = prevStatusRef.current
          if (next === "working") {
            // validating -> working means a retry; a fresh job starts from
            // idle/done and resets the counter.
            if (prev === "validating") setRetryCount((n) => n + 1)
            else if (prev === "idle" || prev === "done") setRetryCount(0)
            setWorkingLines(typeof m.lines === "number" ? m.lines : 0)
            setAgentStatus("working")
          } else if (next === "validating") {
            setAgentStatus("validating")
          } else if (next === "idle") {
            setWorkingLines(0)
            setAgentStatus("idle")
          } else if (next === "done") {
            setLastDone({
              success: m.success === true,
              message: typeof m.message === "string" ? m.message : undefined,
            })
          }
          prevStatusRef.current = next
          break
        }
```

- [ ] **Step 5: Replace the `snapshot` case**

Replace:

```ts
        case "snapshot":
          if (isAgentStatus(m.agent_status)) setAgentStatus(m.agent_status)
          if (typeof m.message === "string") setAgentMessage(m.message)
          if (m.app && typeof m.app === "object") {
            const a = m.app as { code?: unknown; version?: unknown }
            if (typeof a.code === "string" && typeof a.version === "number") {
              setCurrentApp({ code: a.code, version: a.version })
            }
          }
          if (typeof m.css === "string") setCss(m.css)
          break
```

with:

```ts
        case "snapshot":
          if (isAgentStatus(m.agent_status) && m.agent_status !== "done") {
            setAgentStatus(m.agent_status)
            prevStatusRef.current = m.agent_status
          }
          if (typeof m.lines === "number") setWorkingLines(m.lines)
          if (m.app && typeof m.app === "object") {
            const a = m.app as { code?: unknown; version?: unknown }
            if (typeof a.code === "string" && typeof a.version === "number") {
              setCurrentApp({ code: a.code, version: a.version })
            }
          }
          if (typeof m.css === "string") setCss(m.css)
          break
```

- [ ] **Step 6: Update the `isAgentStatus` guard**

Replace:

```ts
function isAgentStatus(s: unknown): s is AgentStatus {
  return s === "idle" || s === "working" || s === "done" || s === "error"
}
```

with:

```ts
function isAgentStatus(s: unknown): s is AgentStatus {
  return s === "idle" || s === "working" || s === "validating" || s === "done"
}
```

- [ ] **Step 7: Update the hook's return value**

Replace:

```ts
  return { status, transcript, agentStatus, agentMessage, currentApp, css, setFrameHandler }
```

with:

```ts
  return {
    status, transcript, agentStatus, workingLines, retryCount,
    lastDone, dismissDone, currentApp, css, setFrameHandler,
  }
```

- [ ] **Step 8: Commit**

```bash
git add src/hooks/useVoiceMonitor.ts
git commit -m "feat(m5stick-voice): monitor hook tracks line count, retries, done toast"
```

---

## Task 9: Wire the route and header

**Files:**
- Modify: `src/components/Header.tsx`
- Modify: `src/routes/devices.$deviceId.tsx`

- [ ] **Step 1: Replace `Header.tsx`**

Replace the entire contents of `src/components/Header.tsx` with:

```tsx
import { StatusPill, type AgentStatus } from "./StatusPill"

interface Props {
  deviceId: string
  status: string
  agentStatus: AgentStatus
  workingLines: number
  retryCount: number
}

export function Header({ deviceId, status, agentStatus, workingLines, retryCount }: Props) {
  return (
    <header style={{
      padding: "10px 16px",
      textShadow: "0 1px 3px rgba(0,0,0,.6)",
    }}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
        <h1 style={{ fontSize: 14, margin: 0, opacity: 0.7, fontWeight: 600 }}>
          m5stick-voice · {deviceId}
        </h1>
        <StatusPill status={agentStatus} lines={workingLines} retryCount={retryCount} />
      </div>
      <div style={{ fontSize: 12, opacity: 0.55, marginTop: 2 }}>{status}</div>
    </header>
  )
}
```

- [ ] **Step 2: Update the route**

In `src/routes/devices.$deviceId.tsx`, add the `DoneToast` import after the other component imports:

```tsx
import { DoneToast } from "../components/DoneToast"
```

Replace the hook destructuring:

```tsx
  const {
    status, transcript, agentStatus, agentMessage, currentApp, css, setFrameHandler,
  } = useVoiceMonitor(deviceId)
```

with:

```tsx
  const {
    status, transcript, agentStatus, workingLines, retryCount,
    lastDone, dismissDone, currentApp, css, setFrameHandler,
  } = useVoiceMonitor(deviceId)
```

Replace the `<Header .../>` element:

```tsx
      <Header
        deviceId={deviceId}
        status={status}
        agentStatus={agentStatus}
        agentMessage={agentMessage}
      />
```

with:

```tsx
      <Header
        deviceId={deviceId}
        status={status}
        agentStatus={agentStatus}
        workingLines={workingLines}
        retryCount={retryCount}
      />
```

Then add the toast just before the closing `</>`, after `<MicSim ... />`:

```tsx
      {lastDone && (
        <DoneToast
          success={lastDone.success}
          message={lastDone.message}
          onDismiss={dismissDone}
        />
      )}
```

- [ ] **Step 3: Catch any stragglers referencing the removed fields**

Run: `grep -rn "agentMessage\|\"error\"\|'error'" src/`
Expected: no remaining references in `components/`, `hooks/`, or `routes/` tied to the old status model. (`onOpenAIEvent` / `console.error` strings in `voice-agent.ts` are unrelated — leave them.)

- [ ] **Step 4: Type-check and build**

Run: `npm run typecheck && npm run build`
Expected: both PASS.

- [ ] **Step 5: Commit**

```bash
git add src/components/Header.tsx src/routes/devices.\$deviceId.tsx
git commit -m "feat(m5stick-voice): render line count in header and done toast in viewer"
```

---

## Task 10: Full verification

**Files:** none (verification only)

- [ ] **Step 1: Run the whole server check suite**

Run: `npm run typecheck && npm test && npm run build`
Expected: all PASS.

- [ ] **Step 2: Browser smoke test (local dev)**

Run: `npm run dev`, open the viewer for a device id, and trigger a `create_app`
(via the voice path or by however the project drives it locally). Confirm:
- the pill goes `working` → shows a climbing `working · N lines` → `validating`
- on success a green "App ready" toast appears and is dismissable; the pill returns to `idle`
- forcing a validation failure shows `working · retry 1` after the first `validating`, and a red error toast on final failure
- refreshing the tab mid-job restores `working`/`validating` (never a stale toast)

- [ ] **Step 3: Hardware verification (REQUIRED before any device-facing claim)**

Per repo policy, firmware/device behaviour is verified on real hardware before
the work is called done. With a physical M5Stick connected, run a `create_app`
and confirm the device receives the `{type:"agent_status",...}` frames alongside
the running app, and that the `WORKING_APP` placeholder still appears. Pause here
for Matt's on-board test before treating the device path as complete.

- [ ] **Step 4: Final confirmation**

Report the exact command output from Step 1 and the observations from Steps 2–3.
Do not claim completion without this evidence.

---

## Self-review notes

- **Spec coverage:** unified `idle|working|validating|done` model (Tasks 4, 6, 8); per-line time-throttled progress (Tasks 2, 3); broadcast to monitors + devices (Task 4); retry inferred client-side from `validating→working` (Task 8); `done` as transient toast then `idle` (Tasks 5, 7, 8); `WORKING_APP` push unchanged (untouched in Task 5); firmware contract documented, not implemented (file-structure note + Task 10). `lastLine` intentionally omitted.
- **Type consistency:** `AgentStatus = "idle"|"working"|"validating"|"done"` is defined identically in `voice-agent.ts` (Task 4) and `StatusPill.tsx` (Task 6) and imported by the hook/header. `setAgentStatus(state, { lines?, success?, message? })`, `finishJob(jobId, success: boolean, message?)`, and `callCodegenChat(..., onProgress)` signatures match every call site. `createLineProgress`, `parseSSELine`, `countLines`, `ValidationResult` all referenced as defined.
- **Placeholders:** none — every code step is complete.
```
