interface Props {
  success: boolean
  message?: string
  onDismiss: () => void
}

/** Dismissable bottom-centre toast shown when a coding job finishes. */
export function DoneToast({ success, message, onDismiss }: Props) {
  return (
    <div style={{
      position: "fixed", left: "50%", bottom: 24, transform: "translateX(-50%)",
      zIndex: 50, maxWidth: 420,
      padding: "10px 14px",
      display: "flex", alignItems: "flex-start", gap: 10,
      fontSize: 13,
      fontFamily: "ui-monospace, SFMono-Regular, Menlo, monospace",
      background: success ? "rgba(63,208,125,.12)" : "rgba(224,83,63,.15)",
      color: success ? "#a8e6c1" : "#f3a298",
      border: `1px solid ${success ? "rgba(63,208,125,.4)" : "rgba(224,83,63,.4)"}`,
      borderRadius: 6,
      boxShadow: "0 4px 16px rgba(0,0,0,.4)",
      whiteSpace: "pre-wrap", wordBreak: "break-word",
    }}>
      <span style={{ flex: 1 }}>
        {success ? "App ready" : `error: ${message ?? "unknown error"}`}
      </span>
      <button onClick={onDismiss} aria-label="Dismiss" style={{
        background: "none", border: "none", color: "inherit",
        cursor: "pointer", fontSize: 14, lineHeight: 1, opacity: 0.7, padding: 0,
      }}>×</button>
    </div>
  )
}
