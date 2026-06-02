export type AgentStatus = "idle" | "working" | "done" | "error"

interface Props {
  status: AgentStatus
  message?: string
}

/** Coarse status pill — see spec section "Status pill". M1 is always "idle". */
export function StatusPill({ status, message }: Props) {
  const styles: Record<AgentStatus, { dot: string; label: string }> = {
    idle:    { dot: "#666",   label: "idle" },
    working: { dot: "#e0c542", label: "working" },
    done:    { dot: "#3fd07d", label: "done" },
    error:   { dot: "#e0533f", label: "error" },
  }
  const s = styles[status]
  return (
    <span title={message ?? ""} style={{
      display: "inline-flex", alignItems: "center", gap: 6,
      fontSize: 12, opacity: 0.8,
    }}>
      <span style={{
        width: 8, height: 8, borderRadius: 4, background: s.dot,
        animation: status === "working" ? "pulse 1s ease-in-out infinite" : undefined,
      }} />
      <span>{s.label}</span>
      <style>{`@keyframes pulse { 50% { opacity: 0.4 } }`}</style>
    </span>
  )
}
