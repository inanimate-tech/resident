import { StatusPill, type AgentStatus } from "./StatusPill"

interface Props {
  deviceId: string
  status: string
  agentStatus: AgentStatus
  agentMessage?: string
}

export function Header({ deviceId, status, agentStatus, agentMessage }: Props) {
  const showMessage = !!agentMessage && (agentStatus === "error" || agentStatus === "done")
  return (
    <header style={{
      padding: "10px 16px",
      textShadow: "0 1px 3px rgba(0,0,0,.6)",
    }}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
        <h1 style={{ fontSize: 14, margin: 0, opacity: 0.7, fontWeight: 600 }}>
          m5stick-voice · {deviceId}
        </h1>
        <StatusPill status={agentStatus} message={agentMessage} />
      </div>
      <div style={{ fontSize: 12, opacity: 0.55, marginTop: 2 }}>{status}</div>
      {showMessage && (
        <div style={{
          marginTop: 4, padding: "4px 8px",
          fontSize: 12, fontFamily: "ui-monospace, SFMono-Regular, Menlo, monospace",
          background: agentStatus === "error" ? "rgba(224,83,63,.15)" : "rgba(63,208,125,.10)",
          color: agentStatus === "error" ? "#f3a298" : "#a8e6c1",
          border: `1px solid ${agentStatus === "error" ? "rgba(224,83,63,.4)" : "rgba(63,208,125,.3)"}`,
          borderRadius: 4,
          whiteSpace: "pre-wrap", wordBreak: "break-word",
          maxHeight: 120, overflowY: "auto",
        }}>
          {agentStatus === "error" ? "error: " : ""}{agentMessage}
        </div>
      )}
    </header>
  )
}
