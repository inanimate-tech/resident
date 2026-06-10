import { StatusPill, type AgentStatus } from "./StatusPill"

interface Props {
  deviceId: string
  status: string
  agentStatus: AgentStatus
  workingLines: number
  retryCount: number
}

export function Header({ deviceId, status, agentStatus, workingLines, retryCount }: Props) {
  return (
    <header style={{
      padding: "10px 16px",
      textShadow: "0 1px 3px rgba(0,0,0,.6)",
    }}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
        <h1 style={{ fontSize: 14, margin: 0, opacity: 0.7, fontWeight: 600 }}>
          m5stick-voice · {deviceId}
        </h1>
        <StatusPill status={agentStatus} lines={workingLines} retryCount={retryCount} />
      </div>
      <div style={{ fontSize: 12, opacity: 0.55, marginTop: 2 }}>{status}</div>
    </header>
  )
}
