import { DeviceAgent } from "@inanimate/resident/cloudflare"
import type { Connection, ConnectionContext, WSMessage } from "agents"
import { z } from "zod"
import { validateLuaCode } from "../lib/lua-validator"
import { DEFAULT_APP } from "../lib/default-app"
import SANDBOX_MD from "../prompts/sandbox.md?raw"
import DEVICE_SKILL_MD from "../prompts/m5stick-device-skill.md?raw"

/** base64 without Buffer/node types — frames are ~1KB so one pass is fine. */
function bytesToBase64(bytes: Uint8Array): string {
  let binary = ""
  const chunk = 0x8000
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunk))
  }
  return btoa(binary)
}

/**
 * Linear-interpolate 16 kHz PCM16 up to 24 kHz (3:2). The OpenAI realtime
 * transcription input requires rate >= 24000; the device captures at 16000.
 */
function upsample16to24(pcm: Int16Array): Int16Array {
  const outLen = Math.floor((pcm.length * 3) / 2)
  const out = new Int16Array(outLen)
  for (let i = 0; i < outLen; i++) {
    const srcPos = (i * 2) / 3
    const i0 = Math.floor(srcPos)
    const i1 = i0 + 1 < pcm.length ? i0 + 1 : pcm.length - 1
    const frac = srcPos - i0
    out[i] = Math.round(pcm[i0] * (1 - frac) + pcm[i1] * frac)
  }
  return out
}

const REALTIME_MODEL = "gpt-realtime-2"
const CODEGEN_MODEL = "gpt-5"

const SYSTEM_PROMPT = `You can do two things via tool calls. Pick the right one based on the user's request:

1. **apply_css** — repaint the WEB PAGE BACKGROUND. Use when the user asks about the page, the website, "the background", or asks for a colour/pattern/animation that fills the screen behind the content. The page has a full-viewport #bg element. Provide a COMPLETE stylesheet; it replaces the previous one. Style #bg and body, define @keyframes, use gradients, embed SVG data URIs.

2. **create_app** — generate and run a Lua app on the SIMULATED M5StickC DEVICE shown on the page (a small 240×135 screen with two buttons). Use when the user asks for something to happen "on the device", "on the m5stick", "on the screen", asks for a clock, a counter, a game, a bouncing ball, anything interactive. Returns asynchronously — the coding agent writes Lua and pushes it; the user sees status in the UI.

3. **push_app** — push the app CURRENTLY SHOWN IN THE SIMULATOR to the user's PHYSICAL device. Use when the user says to push / send / deploy / load the app onto the device, the stick, or the hardware (e.g. "ok push app", "send it to my stick"). No arguments — it sends whatever the simulator is showing.

For ambiguous requests like "show stripes", default to apply_css (the page) unless the user mentioned the device. Prefer acting through a tool over talking; keep spoken replies brief.`

const CODEGEN_SYSTEM = `${SANDBOX_MD}\n\n---\n\n${DEVICE_SKILL_MD}\n\n---\n\nWrite a complete Lua app for the M5StickC Plus2 matching the user's description. Use only the documented APIs. Return ONLY Lua source — no commentary, no markdown fences.`

type AgentStatus = "idle" | "working" | "done" | "error"

interface CurrentApp {
  code: string
  version: number
}

const CreateAppArgs = z.object({ description: z.string().min(1).max(500) })

export class VoiceAgent extends DeviceAgent<Env> {
  private openai?: WebSocket
  private openaiReady = false
  private openaiConnecting?: Promise<void>
  private warnedNoKey = false
  private openaiNextAttempt = 0 // backoff so repeated connect failures don't flood logs
  private commitTimer?: ReturnType<typeof setTimeout>

  // M2 codegen state.
  private agentStatus: AgentStatus = "idle"
  private agentMessage?: string
  private currentApp?: CurrentApp
  private appVersion = 0
  private codingAbort?: AbortController
  // M1 background state (apply_css).
  private currentCss = ""

  onConnect(connection: Connection, ctx: ConnectionContext): void {
    super.onConnect(connection, ctx)
    // Send a snapshot to monitor connections so a refreshed tab restores
    // the current status, running app, and painted background.
    const url = new URL(ctx.request.url)
    if (url.searchParams.get("monitor") === "1") {
      connection.send(JSON.stringify({
        type: "snapshot",
        agent_status: this.agentStatus,
        message: this.agentMessage,
        app: this.currentApp,
        css: this.currentCss,
      }))
    }
  }

  async onMessage(connection: Connection, data: WSMessage): Promise<void> {
    if (data instanceof ArrayBuffer) {
      // Binary audio frame from the device: fan out to monitors for the FFT,
      // and feed the OpenAI transcription bridge.
      for (const m of this.getConnections("monitor")) m.send(data)
      await this.appendAudio(data)
      return
    }
    await super.onMessage(connection, data)
  }

  onClose(connection: Connection): void {
    super.onClose(connection)
    if (Array.from(this.getConnections("device")).length === 0) {
      this.closeOpenAI()
    }
  }

  async onRequest(request: Request): Promise<Response> {
    return super.onRequest(request)
  }

  // ---- OpenAI Realtime transcription bridge ----

  private async ensureOpenAI(): Promise<void> {
    if (this.openai && this.openaiReady) return
    if (this.openaiConnecting) return this.openaiConnecting

    const now = Date.now()
    if (now < this.openaiNextAttempt) throw new Error("openai backoff")
    this.openaiNextAttempt = now + 2000

    this.openaiConnecting = (async () => {
      const key = this.env.OPENAI_API_KEY
      if (!key) {
        if (!this.warnedNoKey) {
          console.error("[voice] OPENAI_API_KEY not set — transcription disabled")
          this.warnedNoKey = true
        }
        throw new Error("missing OPENAI_API_KEY")
      }

      const url = "https://api.openai.com/v1/realtime?model=" + REALTIME_MODEL
      console.log("[voice] opening OpenAI session:", url)
      const resp = await fetch(url, {
        headers: {
          Authorization: `Bearer ${key}`,
          Upgrade: "websocket",
        },
      })
      const ws = resp.webSocket
      if (!ws) {
        let body = ""
        try { body = await resp.text() } catch {}
        console.error("[voice] OpenAI upgrade failed:", resp.status, body.slice(0, 800))
        throw new Error(`OpenAI WS upgrade failed: ${resp.status}`)
      }
      ws.accept()

      ws.addEventListener("message", (e) => this.onOpenAIEvent(e))
      ws.addEventListener("close", (ev) => {
        console.warn("[voice] OpenAI closed:", (ev as CloseEvent).code, (ev as CloseEvent).reason)
        this.openai = undefined; this.openaiReady = false
      })
      ws.addEventListener("error", () => {
        console.error("[voice] OpenAI ws error")
        this.openai = undefined; this.openaiReady = false
      })

      // A Worker's outbound WS is usable immediately after accept().
      ws.send(JSON.stringify({
        type: "session.update",
        session: {
          type: "realtime",
          instructions: SYSTEM_PROMPT,
          output_modalities: ["text"], // silent — we don't forward response audio
          tools: [
            {
              type: "function",
              name: "apply_css",
              description:
                "Repaint the WEB PAGE BACKGROUND. Pass a complete CSS stylesheet that replaces the previous one (style #bg and body, define @keyframes, gradients, SVG data URIs).",
              parameters: {
                type: "object",
                properties: {
                  css: {
                    type: "string",
                    description: "A complete CSS stylesheet. Replaces the previous one entirely.",
                  },
                },
                required: ["css"],
              },
            },
            {
              type: "function",
              name: "create_app",
              description:
                "Generate and run a Lua app on the SIMULATED M5StickC DEVICE (240×135, two buttons). Returns immediately with a job_id; the coding agent reports completion asynchronously.",
              parameters: {
                type: "object",
                properties: {
                  description: {
                    type: "string",
                    description:
                      "A short, concrete description of the app or visual to build (1–500 chars).",
                  },
                },
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
          audio: {
            input: {
              format: { type: "audio/pcm", rate: 24000 },
              transcription: { model: "gpt-realtime-whisper" },
              turn_detection: null,
            },
          },
        },
      }))

      this.openai = ws
      this.openaiReady = true
    })()

    try {
      await this.openaiConnecting
    } finally {
      this.openaiConnecting = undefined
    }
  }

  private async appendAudio(buf: ArrayBuffer): Promise<void> {
    try {
      await this.ensureOpenAI()
    } catch {
      return
    }
    const pcm24 = upsample16to24(new Int16Array(buf))
    const b64 = bytesToBase64(new Uint8Array(pcm24.buffer, pcm24.byteOffset, pcm24.byteLength))
    this.openai!.send(JSON.stringify({
      type: "input_audio_buffer.append",
      audio: b64,
    }))

    if (this.commitTimer) clearTimeout(this.commitTimer)
    this.commitTimer = setTimeout(() => {
      this.commitTimer = undefined
      if (this.openai && this.openaiReady) {
        this.openai.send(JSON.stringify({ type: "input_audio_buffer.commit" }))
        this.openai.send(JSON.stringify({ type: "response.create" }))
        console.log("[voice] committed turn + requested response")
      }
    }, 700)
  }

  private onOpenAIEvent(e: MessageEvent): void {
    if (typeof e.data !== "string") return
    let msg: { type?: string; [k: string]: unknown }
    try { msg = JSON.parse(e.data) } catch { return }
    if (!msg || typeof msg.type !== "string") return

    if (msg.type === "conversation.item.input_audio_transcription.delta") {
      this.toMonitors({ type: "transcript.delta", text: msg.delta ?? "", itemId: msg.item_id })
    } else if (msg.type === "conversation.item.input_audio_transcription.completed") {
      this.toMonitors({ type: "transcript.completed", text: msg.transcript ?? "", itemId: msg.item_id })
    } else if (msg.type === "response.function_call_arguments.done") {
      this.handleFunctionCall(
        msg.name as string,
        msg.call_id as string,
        msg.arguments as string,
      )
    } else if (msg.type === "error") {
      console.error("[voice] OpenAI error:", JSON.stringify(msg.error ?? msg))
    }
  }

  // ---- Tool dispatch ----

  private handleFunctionCall(name: string, callId: string, argsJson: string): void {
    if (name === "apply_css") {
      this.handleApplyCss(callId, argsJson)
      return
    }
    if (name === "push_app") {
      this.handlePushApp(callId)
      return
    }
    if (name !== "create_app") {
      console.warn("[voice] unknown tool call:", name)
      this.sendToolResult(callId, { ok: false, error: `unknown tool: ${name}` })
      return
    }

    let parsed: { description: string }
    try {
      parsed = CreateAppArgs.parse(JSON.parse(argsJson))
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      this.sendToolResult(callId, { status: "error", reason: msg })
      return
    }

    const jobId = crypto.randomUUID()

    // Cancel any in-flight job.
    this.codingAbort?.abort()
    this.codingAbort = new AbortController()

    // Tell the model we've started.
    this.sendToolResult(callId, { status: "started", job_id: jobId })
    if (this.openai && this.openaiReady) {
      this.openai.send(JSON.stringify({ type: "response.create" }))
    }

    // Tell the viewer.
    this.setAgentStatus("working", undefined)

    // Fire-and-forget.
    this.ctx.waitUntil(this.runCodingJob(jobId, parsed.description, this.codingAbort.signal))
  }

  private handleApplyCss(callId: string, argsJson: string): void {
    let css = ""
    try {
      const parsed = JSON.parse(argsJson) as { css?: unknown }
      css = typeof parsed.css === "string" ? parsed.css : ""
    } catch {
      this.sendToolResult(callId, { ok: false, error: "bad arguments JSON" })
      return
    }
    console.log("[voice] apply_css:", css.length, "chars")
    this.currentCss = css
    this.toMonitors({ type: "css", css })
    this.sendToolResult(callId, { ok: true })
    if (this.openai && this.openaiReady) {
      this.openai.send(JSON.stringify({ type: "response.create" }))
    }
  }

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
    if (!this.openai || !this.openaiReady) return
    this.openai.send(JSON.stringify({
      type: "conversation.item.create",
      item: { type: "function_call_output", call_id: callId, output: JSON.stringify(payload) },
    }))
  }

  private async runCodingJob(
    jobId: string,
    description: string,
    signal: AbortSignal,
  ): Promise<void> {
    try {
      let code = await this.callCodegenChat(description, [], signal)
      let validation = await validateLuaCode(code)

      if (!validation.ok) {
        console.warn("[voice] codegen v1 validation failed:", validation.error)
        code = await this.callCodegenChat(description, [
          { role: "assistant", content: code },
          {
            role: "user",
            content: `That code failed validation with: ${validation.error}. Fix it. Return only Lua, no commentary.`,
          },
        ], signal)
        validation = await validateLuaCode(code)
      }

      if (signal.aborted) return

      if (!validation.ok) {
        this.finishJob(jobId, "error", validation.error ?? "validation failed")
        return
      }

      this.appVersion += 1
      this.currentApp = { code, version: this.appVersion }
      this.toMonitors({ type: "app", code, version: this.appVersion })
      this.finishJob(jobId, "done", undefined)
    } catch (err) {
      if (signal.aborted) return
      const msg = err instanceof Error ? err.message : String(err)
      this.finishJob(jobId, "error", msg)
    }
  }

  private async callCodegenChat(
    description: string,
    followups: { role: "assistant" | "user"; content: string }[],
    signal: AbortSignal,
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
      body: JSON.stringify({ model: CODEGEN_MODEL, messages }),
      signal,
    })
    if (!resp.ok) {
      const body = await resp.text().catch(() => "")
      throw new Error(`OpenAI ${resp.status}: ${body.slice(0, 400)}`)
    }
    const data = await resp.json() as {
      choices?: Array<{ message?: { content?: string } }>
    }
    const content = data.choices?.[0]?.message?.content ?? ""
    return content.replace(/^```(?:lua)?\s*/i, "").replace(/```\s*$/i, "").trim()
  }

  private finishJob(jobId: string, state: "done" | "error", message: string | undefined): void {
    this.setAgentStatus(state, message)
    if (this.openai && this.openaiReady) {
      const content = state === "done"
        ? `[create_app jobId=${jobId}] completed successfully`
        : `[create_app jobId=${jobId}] failed: ${message ?? "unknown error"}`
      // Inject a user-role message so the realtime model sees the outcome on
      // the next turn. (Realtime's conversation.item.create historically only
      // accepts user/assistant on `message` items; we mark with a [system] tag.)
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

  private setAgentStatus(state: AgentStatus, message: string | undefined): void {
    this.agentStatus = state
    this.agentMessage = message
    this.toMonitors({ type: "agent_status", state, message })
  }

  private toMonitors(obj: unknown): void {
    const s = JSON.stringify(obj)
    for (const m of this.getConnections("monitor")) m.send(s)
  }

  private closeOpenAI(): void {
    if (this.commitTimer) { clearTimeout(this.commitTimer); this.commitTimer = undefined }
    try { this.openai?.close() } catch {}
    this.openai = undefined
    this.openaiReady = false
  }
}
