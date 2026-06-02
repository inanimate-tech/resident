import { useStickDevice, SCREEN_W, SCREEN_H } from "../hooks/useStickDevice"

const SCALE = 2

interface Props {
  code: string | null
  version: number
}

export function SimM5Stick({ code, version }: Props) {
  const { setCanvasRef, handleTrigger, error } = useStickDevice({ code })

  return (
    <div style={{
      padding: 12, background: "#000",
      borderTop: "1px solid rgba(255,255,255,.08)",
      borderBottom: "1px solid rgba(255,255,255,.08)",
      display: "flex", flexDirection: "column", alignItems: "center", gap: 8,
    }}>
      <div style={{ position: "relative" }}>
        <canvas
          ref={setCanvasRef}
          width={SCREEN_W}
          height={SCREEN_H}
          style={{
            width: SCREEN_W * SCALE, height: SCREEN_H * SCALE,
            imageRendering: "pixelated", border: "1px solid #333",
            display: "block",
          }}
        />
        {error && (
          <div style={{
            position: "absolute", inset: 0,
            background: "rgba(120,0,0,.7)", color: "#fff",
            padding: 8, fontSize: 12, fontFamily: "monospace",
            whiteSpace: "pre-wrap", overflow: "auto",
          }}>{error}</div>
        )}
      </div>
      <div style={{ display: "flex", gap: 8 }}>
        <button onClick={() => handleTrigger(0)} style={pillBtn}>Btn0</button>
        <button onClick={() => handleTrigger(1)} style={pillBtn}>Btn1</button>
      </div>
      <div style={{ fontSize: 11, opacity: 0.4 }}>app v{version}</div>
    </div>
  )
}

const pillBtn: React.CSSProperties = {
  padding: "4px 12px", borderRadius: 999,
  background: "rgba(180,90,30,.25)", color: "#f0a060",
  border: "1px solid #b65a1e",
  fontFamily: "system-ui, sans-serif", fontSize: 12, letterSpacing: 1,
  textTransform: "uppercase", cursor: "pointer",
}
