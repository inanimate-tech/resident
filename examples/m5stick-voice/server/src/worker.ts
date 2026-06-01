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

  // Build the realtime connection target. If CF_ACCOUNT_ID + AI_GATEWAY_ID are
  // set, route through Cloudflare AI Gateway's realtime WebSocket; otherwise
  // connect to OpenAI directly. GA API → no `OpenAI-Beta` header.
  private buildOpenAIRequest(key: string): { url: string; headers: Record<string, string>; via: string } {
    // Transcription-only session: ?intent=transcription (no session model). The
    // transcription model goes in session.audio.input.transcription.model below.
    const query = "?intent=transcription"
    const headers: Record<string, string> = {
      Authorization: `Bearer ${key}`,
      Upgrade: "websocket",
    }
    const acct = this.env.CF_ACCOUNT_ID
    const gw = this.env.AI_GATEWAY_ID
    // Set OPENAI_DIRECT=1 to force a direct OpenAI connection even when the
    // gateway secrets are set — useful for isolating gateway-side issues.
    if (acct && gw && this.env.OPENAI_DIRECT !== "1") {
      if (this.env.CF_AIG_TOKEN) headers["cf-aig-authorization"] = `Bearer ${this.env.CF_AIG_TOKEN}`
      return {
        url: `https://gateway.ai.cloudflare.com/v1/${acct}/${gw}/openai${query}`,
        headers,
        via: "ai-gateway",
      }
    }
    return { url: `https://api.openai.com/v1/realtime${query}`, headers, via: "direct" }
  }

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

      const req = this.buildOpenAIRequest(key)
      console.log("[voice] opening OpenAI session via", req.via + ":", req.url)
      const resp = await fetch(req.url, { headers: req.headers })
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
          type: "transcription",
          audio: {
            input: {
              format: { type: "audio/pcm", rate: 24000 },
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
    const pcm24 = upsample16to24(new Int16Array(buf))
    const b64 = bytesToBase64(new Uint8Array(pcm24.buffer, pcm24.byteOffset, pcm24.byteLength))
    this.openai!.send(JSON.stringify({
      type: "input_audio_buffer.append",
      audio: b64,
    }))

    // gpt-realtime-whisper has no server VAD, so commit the buffer after a
    // short silence (≈ the button release) to transcribe the turn.
    if (this.commitTimer) clearTimeout(this.commitTimer)
    this.commitTimer = setTimeout(() => {
      this.commitTimer = undefined
      if (this.openai && this.openaiReady) {
        this.openai.send(JSON.stringify({ type: "input_audio_buffer.commit" }))
        console.log("[voice] committed audio buffer after silence")
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
    } else if (msg.type === "error") {
      console.error("[voice] OpenAI error:", JSON.stringify(msg.error ?? msg))
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
