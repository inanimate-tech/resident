import { useEffect, useRef, useState } from "react"
import { useAgent } from "agents/react"
import type { AgentStatus } from "../components/StatusPill"

export interface TranscriptItem {
  id: string
  text: string
  done: boolean
}

export interface CurrentApp {
  code: string
  version: number
}

export interface VoiceMonitor {
  status: string
  transcript: TranscriptItem[]
  agentStatus: AgentStatus
  agentMessage?: string
  currentApp?: CurrentApp
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
  const [agentStatus, setAgentStatus] = useState<AgentStatus>("idle")
  const [agentMessage, setAgentMessage] = useState<string | undefined>(undefined)
  const [currentApp, setCurrentApp] = useState<CurrentApp | undefined>(undefined)
  const [css, setCss] = useState("")
  const frameHandlerRef = useRef<((buf: ArrayBuffer) => void) | null>(null)

  const setFrameHandler: VoiceMonitor["setFrameHandler"] = (cb) => {
    frameHandlerRef.current = cb
  }

  const agent = useAgent({
    agent: "voice-agent",
    name: deviceId,
    query: { monitor: "1" },
    onOpen: () => {
      setStatus("connected")
      try { (agent as unknown as WebSocket).binaryType = "arraybuffer" } catch {}
    },
    onClose: () => setStatus("disconnected — reconnecting…"),
    onMessage: (event) => {
      if (typeof event.data !== "string") {
        if (event.data instanceof ArrayBuffer) {
          frameHandlerRef.current?.(event.data)
        } else if (event.data instanceof Blob) {
          event.data.arrayBuffer().then((buf) => frameHandlerRef.current?.(buf))
        }
        return
      }
      let m: { type?: string; [k: string]: unknown }
      try { m = JSON.parse(event.data) } catch { return }
      if (!m || typeof m.type !== "string") return

      switch (m.type) {
        case "status":
          setStatus(m.deviceConnected ? "device connected" : "waiting for device…")
          break
        case "transcript.delta":
          setTranscript((prev) => upsertItem(prev, String(m.itemId ?? "_cur"), String(m.text ?? ""), false))
          break
        case "transcript.completed":
          setTranscript((prev) => upsertItem(prev, String(m.itemId ?? "_cur"), String(m.text ?? ""), true, true))
          break
        case "agent_status":
          if (isAgentStatus(m.state)) setAgentStatus(m.state)
          setAgentMessage(typeof m.message === "string" ? m.message : undefined)
          break
        case "app":
          if (typeof m.code === "string" && typeof m.version === "number") {
            setCurrentApp({ code: m.code, version: m.version })
          }
          break
        case "css":
          if (typeof m.css === "string") setCss(m.css)
          break
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
      }
    },
  })

  useEffect(() => { void agent }, [agent])

  return { status, transcript, agentStatus, agentMessage, currentApp, css, setFrameHandler }
}

function isAgentStatus(s: unknown): s is AgentStatus {
  return s === "idle" || s === "working" || s === "done" || s === "error"
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
