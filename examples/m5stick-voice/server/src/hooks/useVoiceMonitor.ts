import { useEffect, useRef, useState } from "react"
import { useAgent } from "agents/react"

export interface TranscriptItem {
  id: string
  text: string
  done: boolean
}

export interface VoiceMonitor {
  status: string
  transcript: TranscriptItem[]
  css: string
  setFrameHandler: (cb: ((buf: ArrayBuffer) => void) | null) => void
}

/**
 * Owns the monitor-tagged WebSocket against the VoiceAgent DO. Decodes the
 * JSON message types we know about; binary frames are forwarded to a
 * caller-supplied handler so the FFT component can process them without
 * triggering a React render per frame.
 */
export function useVoiceMonitor(deviceId: string): VoiceMonitor {
  const [status, setStatus] = useState("connecting…")
  const [transcript, setTranscript] = useState<TranscriptItem[]>([])
  const [css, setCss] = useState("")
  const frameHandlerRef = useRef<((buf: ArrayBuffer) => void) | null>(null)

  const setFrameHandler: VoiceMonitor["setFrameHandler"] = (cb) => {
    frameHandlerRef.current = cb
  }

  const agent = useAgent({
    agent: "voice-agent",
    name: deviceId,
    query: { monitor: "1" },
    onOpen: () => setStatus("connected"),
    onClose: () => setStatus("disconnected — reconnecting…"),
    onMessage: (event) => {
      if (typeof event.data !== "string") {
        if (event.data instanceof ArrayBuffer) frameHandlerRef.current?.(event.data)
        return
      }
      let m: { type?: string; [k: string]: unknown }
      try { m = JSON.parse(event.data) } catch { return }
      if (!m || typeof m.type !== "string") return

      if (m.type === "status") {
        setStatus(m.deviceConnected ? "device connected" : "waiting for device…")
      } else if (m.type === "transcript.delta") {
        setTranscript((prev) => upsertItem(prev, String(m.itemId ?? "_cur"), String(m.text ?? ""), false))
      } else if (m.type === "transcript.completed") {
        setTranscript((prev) => upsertItem(prev, String(m.itemId ?? "_cur"), String(m.text ?? ""), true, true))
      } else if (m.type === "css") {
        setCss(String(m.css ?? ""))
      }
    },
  })

  useEffect(() => { void agent }, [agent])

  return { status, transcript, css, setFrameHandler }
}

function upsertItem(
  prev: TranscriptItem[],
  id: string,
  text: string,
  done: boolean,
  replace = false,
): TranscriptItem[] {
  const idx = prev.findIndex((it) => it.id === id)
  if (idx === -1) return [...prev, { id, text, done }]
  const next = prev.slice()
  next[idx] = { id, text: replace ? text : next[idx].text + text, done }
  return next
}
