import { useEffect, useRef, useState } from "react"

interface Props {
  deviceId: string
}

const FRAME_SIZE = 512 // 32 ms @ 16 kHz, matching the physical device's frame size
const TARGET_RATE = 16000

/**
 * Floating PTT widget. When enabled, opens a bare WebSocket to /devices/<id>
 * — the same path the physical M5StickC uses — and forwards mic audio as
 * 16 kHz PCM16 binary frames while spacebar is held. The DO can't tell the
 * difference between this and a real device, so it drives the realtime
 * transcription pipeline end-to-end for local testing without hardware.
 */
export function MicSim({ deviceId }: Props) {
  const [enabled, setEnabled] = useState(false)
  const [holding, setHolding] = useState(false)
  const [wsState, setWsState] = useState<"idle" | "open" | "closed" | "error">("idle")
  const [err, setErr] = useState<string | null>(null)

  const wsRef = useRef<WebSocket | null>(null)
  const audioCtxRef = useRef<AudioContext | null>(null)
  const streamRef = useRef<MediaStream | null>(null)
  const sourceRef = useRef<MediaStreamAudioSourceNode | null>(null)
  const processorRef = useRef<ScriptProcessorNode | null>(null)
  const sampleBufRef = useRef<Int16Array>(new Int16Array(FRAME_SIZE))
  const sampleFillRef = useRef(0)
  const holdingRef = useRef(false)

  useEffect(() => { holdingRef.current = holding }, [holding])

  // Open WS once the user clicks "Enable browser mic".
  useEffect(() => {
    if (!enabled) return
    const proto = location.protocol === "https:" ? "wss:" : "ws:"
    const url = `${proto}//${location.host}/devices/${encodeURIComponent(deviceId)}`
    const ws = new WebSocket(url)
    ws.binaryType = "arraybuffer"
    ws.onopen = () => setWsState("open")
    ws.onclose = () => setWsState("closed")
    ws.onerror = () => setWsState("error")
    wsRef.current = ws
    return () => {
      try { ws.close() } catch {}
      wsRef.current = null
    }
  }, [enabled, deviceId])

  // Spacebar press/release while enabled.
  useEffect(() => {
    if (!enabled) return
    const down = (e: KeyboardEvent) => {
      if (e.code !== "Space" || e.repeat) return
      // Don't hijack space in form inputs.
      const t = e.target as HTMLElement | null
      if (t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.isContentEditable)) return
      e.preventDefault()
      setHolding(true)
    }
    const up = (e: KeyboardEvent) => {
      if (e.code !== "Space") return
      e.preventDefault()
      setHolding(false)
    }
    window.addEventListener("keydown", down)
    window.addEventListener("keyup", up)
    return () => {
      window.removeEventListener("keydown", down)
      window.removeEventListener("keyup", up)
    }
  }, [enabled])

  // While holding, capture and stream. We keep the AudioContext alive for the
  // life of `enabled` and only gate sending in the processor callback — that
  // way the first space-press doesn't have to spin up the whole pipeline.
  useEffect(() => {
    if (!enabled) return
    let cancelled = false
    ;(async () => {
      try {
        const stream = await navigator.mediaDevices.getUserMedia({
          audio: { channelCount: 1, echoCancellation: false, noiseSuppression: false },
        })
        if (cancelled) { stream.getTracks().forEach((t) => t.stop()); return }
        streamRef.current = stream

        const ctx = new AudioContext()
        audioCtxRef.current = ctx
        const inRate = ctx.sampleRate

        const source = ctx.createMediaStreamSource(stream)
        sourceRef.current = source

        const proc = ctx.createScriptProcessor(4096, 1, 1)
        processorRef.current = proc
        proc.onaudioprocess = (e) => {
          if (!holdingRef.current) return
          const ws = wsRef.current
          if (!ws || ws.readyState !== WebSocket.OPEN) return

          const inBuf = e.inputBuffer.getChannelData(0)
          const ratio = inRate / TARGET_RATE
          const outLen = Math.floor(inBuf.length / ratio)
          const buf = sampleBufRef.current
          let fill = sampleFillRef.current

          for (let i = 0; i < outLen; i++) {
            const s = inBuf[Math.floor(i * ratio)]
            const v = Math.max(-1, Math.min(1, s)) * 32767
            buf[fill++] = v < 0 ? Math.ceil(v) : Math.floor(v)
            if (fill === FRAME_SIZE) {
              // Send a copy — the underlying buffer is reused for the next frame.
              ws.send(buf.slice().buffer)
              fill = 0
            }
          }
          sampleFillRef.current = fill
        }

        source.connect(proc)
        // Connecting to destination is required for the script processor to run
        // in some browsers, even though we don't want monitor output. Mute it.
        const mute = ctx.createGain()
        mute.gain.value = 0
        proc.connect(mute)
        mute.connect(ctx.destination)

        if (ctx.state === "suspended") await ctx.resume()
      } catch (e) {
        setErr(e instanceof Error ? e.message : String(e))
      }
    })()
    return () => {
      cancelled = true
      try { processorRef.current?.disconnect() } catch {}
      try { sourceRef.current?.disconnect() } catch {}
      try { streamRef.current?.getTracks().forEach((t) => t.stop()) } catch {}
      try { audioCtxRef.current?.close() } catch {}
      processorRef.current = null
      sourceRef.current = null
      streamRef.current = null
      audioCtxRef.current = null
      sampleFillRef.current = 0
    }
  }, [enabled])

  return (
    <div style={{
      position: "fixed", right: 16, bottom: 16, zIndex: 100,
      background: "rgba(11,11,16,.85)",
      border: "1px solid rgba(255,255,255,.12)",
      borderRadius: 10, padding: 12,
      color: "#e6e6f0", fontFamily: "system-ui, sans-serif", fontSize: 13,
      boxShadow: "0 4px 14px rgba(0,0,0,.4)",
      minWidth: 200,
    }}>
      {!enabled ? (
        <button
          onClick={() => setEnabled(true)}
          style={pillBtn}
        >🎙 Enable browser mic</button>
      ) : (
        <>
          <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
            <div style={{
              width: 10, height: 10, borderRadius: 5,
              background: holding ? "#e0533f" : (wsState === "open" ? "#3fd07d" : "#666"),
              boxShadow: holding ? "0 0 8px #e0533f" : undefined,
              transition: "background .1s",
            }} />
            <div style={{ fontSize: 12, opacity: holding ? 1 : 0.7 }}>
              {holding ? "speaking…" : "hold SPACE to talk"}
            </div>
          </div>
          <div style={{ marginTop: 6, fontSize: 11, opacity: 0.5 }}>
            ws: {wsState}{err ? ` · ${err}` : ""}
          </div>
        </>
      )}
    </div>
  )
}

const pillBtn: React.CSSProperties = {
  padding: "6px 12px", borderRadius: 999,
  background: "rgba(180,90,30,.25)", color: "#f0a060",
  border: "1px solid #b65a1e",
  fontFamily: "system-ui, sans-serif", fontSize: 12, letterSpacing: 1,
  textTransform: "uppercase", cursor: "pointer",
}
