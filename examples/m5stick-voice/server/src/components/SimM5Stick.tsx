import { useState } from "react"
import { useStickDevice, SCREEN_W, SCREEN_H } from "../hooks/useStickDevice"
import { Sim3DStick } from "./Sim3DStick"

interface Props {
  code: string | null
  version: number
}

export function SimM5Stick({ code, version }: Props) {
  // Track the offscreen canvas via state so Sim3DStick re-renders when it
  // mounts (CanvasTexture needs the actual HTMLCanvasElement, not a ref).
  const [canvasEl, setCanvasEl] = useState<HTMLCanvasElement | null>(null)
  const { setCanvasRef, handleTrigger, error } = useStickDevice({ code })

  // Forward the canvas to both the runtime (paint target) and the 3D scene
  // (texture source).
  const onCanvasRef = (el: HTMLCanvasElement | null) => {
    setCanvasRef(el)
    setCanvasEl(el)
  }

  return (
    <div style={{
      padding: 12,
      display: "flex", flexDirection: "column", alignItems: "center", gap: 8,
    }}>
      {/* Offscreen canvas — Lua paints into this; Sim3DStick samples it. */}
      <canvas
        ref={onCanvasRef}
        width={SCREEN_W}
        height={SCREEN_H}
        style={{ display: "none" }}
      />

      <div style={{ position: "relative", width: "100%", maxWidth: 520 }}>
        <Sim3DStick
          screenCanvas={canvasEl}
          onPressBtn0={() => handleTrigger(0)}
          onPressBtn1={() => handleTrigger(1)}
        />
        {error && (
          <div style={{
            position: "absolute", inset: 0,
            background: "rgba(120,0,0,.7)", color: "#fff",
            padding: 8, fontSize: 12, fontFamily: "monospace",
            whiteSpace: "pre-wrap", overflow: "auto",
            borderRadius: 4,
          }}>{error}</div>
        )}
      </div>
      <div style={{ fontSize: 11, opacity: 0.4 }}>app v{version}</div>
    </div>
  )
}
