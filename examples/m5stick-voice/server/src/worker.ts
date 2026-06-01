import { DeviceAgent, routeDeviceRequest } from "@inanimate/resident/cloudflare"
import type { Connection, WSMessage } from "agents"
import { viewerHtml } from "./viewer"

/** base64 without Buffer/node types — frames are ~1KB so one pass is fine. */
function toBase64(buf: ArrayBuffer): string {
  const bytes = new Uint8Array(buf)
  let binary = ""
  const chunk = 0x8000
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunk))
  }
  return btoa(binary)
}

export class VoiceAgent extends DeviceAgent<Env> {
  private openai?: WebSocket
  private openaiReady = false
  private openaiConnecting?: Promise<void>
  private warnedNoKey = false

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

    this.openaiConnecting = (async () => {
      const key = this.env.OPENAI_API_KEY
      if (!key) {
        if (!this.warnedNoKey) {
          console.error("[voice] OPENAI_API_KEY not set — transcription disabled")
          this.warnedNoKey = true
        }
        throw new Error("missing OPENAI_API_KEY")
      }

      const resp = await fetch(
        "https://api.openai.com/v1/realtime?model=gpt-realtime-whisper",
        {
          headers: {
            Authorization: `Bearer ${key}`,
            "OpenAI-Beta": "realtime=v1",
            Upgrade: "websocket",
          },
        },
      )
      const ws = resp.webSocket
      if (!ws) throw new Error(`OpenAI WS upgrade failed: ${resp.status}`)
      ws.accept()

      ws.addEventListener("message", (e) => this.onOpenAIEvent(e))
      ws.addEventListener("close", () => { this.openai = undefined; this.openaiReady = false })
      ws.addEventListener("error", () => { this.openai = undefined; this.openaiReady = false })

      // A Worker's outbound WS is usable immediately after accept() (no
      // browser-style "open" event), so configure the transcription session now.
      ws.send(JSON.stringify({
        type: "session.update",
        session: {
          type: "transcription",
          audio: {
            input: {
              format: { type: "audio/pcm", rate: 16000 },
              transcription: { model: "gpt-realtime-whisper", language: "en" },
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
    this.openai!.send(JSON.stringify({
      type: "input_audio_buffer.append",
      audio: toBase64(buf),
    }))
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
    } else if (msg.type === "error") {
      console.error("[voice] OpenAI error:", JSON.stringify(msg.error ?? msg))
    }
  }

  private toMonitors(obj: unknown): void {
    const s = JSON.stringify(obj)
    for (const m of this.getConnections("monitor")) m.send(s)
  }

  private closeOpenAI(): void {
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
