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
