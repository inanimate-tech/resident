import { StatusPill, type AgentStatus } from "./StatusPill"

interface Props {
  deviceId: string
  status: string
  agentStatus: AgentStatus
  agentMessage?: string
}

export function Header({ deviceId, status, agentStatus, agentMessage }: Props) {
  return (
    <header style={{
      padding: "10px 16px",
      background: "rgba(11,11,16,.55)",
      borderBottom: "1px solid rgba(255,255,255,.08)",
    }}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
        <h1 style={{ fontSize: 14, margin: 0, opacity: 0.7, fontWeight: 600 }}>
          m5stick-voice · {deviceId}
        </h1>
        <StatusPill status={agentStatus} message={agentMessage} />
      </div>
      <div style={{ fontSize: 12, opacity: 0.55, marginTop: 2 }}>{status}</div>
    </header>
  )
}
