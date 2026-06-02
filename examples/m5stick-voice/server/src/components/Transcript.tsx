import { useEffect, useRef } from "react"
import type { TranscriptItem } from "../hooks/useVoiceMonitor"

interface Props {
  items: TranscriptItem[]
}

const AT_BOTTOM_THRESHOLD = 40

export function Transcript({ items }: Props) {
  const containerRef = useRef<HTMLDivElement>(null)
  const wasAtBottomRef = useRef(true)

  // Track whether we were at the bottom before this render so we can preserve
  // that on update — same behaviour as the original viewer.
  useEffect(() => {
    const el = containerRef.current
    if (!el) return
    if (wasAtBottomRef.current) el.scrollTop = el.scrollHeight
  })

  const onScroll = () => {
    const el = containerRef.current
    if (!el) return
    wasAtBottomRef.current =
      el.scrollHeight - el.scrollTop - el.clientHeight < AT_BOTTOM_THRESHOLD
  }

  return (
    <div
      ref={containerRef}
      onScroll={onScroll}
      style={{
        flex: 1, overflowY: "auto", padding: 16,
      }}
    >
      {items.map((it) => (
        <p key={it.id}
           style={{
             margin: "0 0 14px", whiteSpace: "pre-wrap",
             textShadow: "0 1px 3px rgba(0,0,0,.6)",
             fontSize: 32, lineHeight: 1.25,
             opacity: it.done ? 1 : 0.5,
           }}>
          {it.text}
        </p>
      ))}
    </div>
  )
}
