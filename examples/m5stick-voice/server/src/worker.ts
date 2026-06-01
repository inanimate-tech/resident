import { DeviceAgent, routeDeviceRequest } from "@inanimate/resident/cloudflare"
import type { Connection, WSMessage } from "agents"
import { viewerHtml } from "./viewer"

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

// The realtime SESSION model (conversational). A constant so swapping
// gpt-realtime-2 / gpt-realtime is one line. The transcription sub-model is set
// separately in session.audio.input.transcription.model.
const REALTIME_MODEL = "gpt-realtime-2"

// System prompt: the model controls the viewer's background via apply_css.
const CSS_INSTRUCTIONS = `You control the background of a web page by calling the apply_css function. The page has a full-viewport element with id "bg" behind the content. When the user asks for a visual change, call apply_css with a COMPLETE CSS stylesheet — it replaces the previous one entirely, so always include everything needed for the current look. You may style #bg and body, define @keyframes for animation, use gradients, and embed repeating patterns via SVG data URIs (background-image: url("data:image/svg+xml,...")). For incremental requests ("faster", "different colour", "invert them"), re-emit the full CSS with that change applied. Prefer acting through the tool over talking; keep any text brief.`

export class VoiceAgent extends DeviceAgent<Env> {
  private openai?: WebSocket
  private openaiReady = false
  private openaiConnecting?: Promise<void>
  private warnedNoKey = false
  private openaiNextAttempt = 0 // backoff so repeated connect failures don't flood logs
  private commitTimer?: ReturnType<typeof setTimeout>

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
    const url = new URL(request.url)
    const subpath = url.pathname.replace(/^\/devices\/[^/]+/, "")
    if ((subpath === "" || subpath === "/") && request.method === "GET") {
      return new Response(viewerHtml(this.name), {
        headers: { "Content-Type": "text/html; charset=utf-8" },
      })
    }
    return super.onRequest(request)
  }

  // ---- OpenAI Realtime transcription bridge ----

  private async ensureOpenAI(): Promise<void> {
    if (this.openai && this.openaiReady) return
    if (this.openaiConnecting) return this.openaiConnecting

    // Debug backoff: if a connect just failed, don't re-attempt (and re-log)
    // on every one of the ~32 audio frames/second.
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

      // Conversational session (gpt-realtime-2). The transcription sub-model is
      // set in session.audio.input.transcription.model below. GA API → no
      // `OpenAI-Beta` header.
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

      // A Worker's outbound WS is usable immediately after accept() (no
      // browser-style "open" event), so configure the transcription session now.
      ws.send(JSON.stringify({
        type: "session.update",
        session: {
          type: "realtime",
          instructions: CSS_INSTRUCTIONS,
          output_modalities: ["text"], // silent — we don't forward response audio
          tools: [
            {
              type: "function",
              name: "apply_css",
              description: "Replace the web page's background stylesheet with the given CSS.",
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
      return // no key / upgrade failed — audio still reached monitors
    }
    const pcm24 = upsample16to24(new Int16Array(buf))
    const b64 = bytesToBase64(new Uint8Array(pcm24.buffer, pcm24.byteOffset, pcm24.byteLength))
    this.openai!.send(JSON.stringify({
      type: "input_audio_buffer.append",
      audio: b64,
    }))

    // No server VAD (turn_detection: null), so on ~0.7s of silence (the button
    // release) commit the turn and ask the model to respond. The commit drives
    // input transcription; the response lets the model call apply_css.
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
    let msg: any
    try { msg = JSON.parse(e.data) } catch { return }
    if (!msg || typeof msg.type !== "string") return

    if (msg.type === "conversation.item.input_audio_transcription.delta") {
      this.toMonitors({ type: "transcript.delta", text: msg.delta ?? "", itemId: msg.item_id })
    } else if (msg.type === "conversation.item.input_audio_transcription.completed") {
      this.toMonitors({ type: "transcript.completed", text: msg.transcript ?? "", itemId: msg.item_id })
    } else if (msg.type === "response.function_call_arguments.done") {
      this.handleFunctionCall(msg.name, msg.call_id, msg.arguments)
    } else if (msg.type === "error") {
      console.error("[voice] OpenAI error:", JSON.stringify(msg.error ?? msg))
    }
  }

  private handleFunctionCall(name: string, callId: string, argsJson: string): void {
    if (name === "apply_css") {
      let css = ""
      try {
        css = JSON.parse(argsJson).css ?? ""
      } catch {
        console.error("[voice] apply_css: bad arguments JSON")
        return
      }
      console.log("[voice] apply_css:", css.length, "chars")
      this.toMonitors({ type: "css", css })
    }
    // Return the tool result so the model can continue / chain another call.
    if (this.openai && this.openaiReady) {
      this.openai.send(JSON.stringify({
        type: "conversation.item.create",
        item: { type: "function_call_output", call_id: callId, output: '{"ok":true}' },
      }))
      this.openai.send(JSON.stringify({ type: "response.create" }))
    }
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

export default {
  async fetch(request: Request, env: Env) {
    const res = await routeDeviceRequest(request, env.VoiceAgent)
    if (res) return res
    return new Response("Not found", { status: 404 })
  },
} satisfies ExportedHandler<Env>
