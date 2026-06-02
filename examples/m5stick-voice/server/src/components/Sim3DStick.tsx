import { useRef, useSyncExternalStore } from "react"
import { Canvas } from "@react-three/fiber"
import * as THREE from "three"
import { RotationHandler } from "./RotationHandler"
import { StickDeviceBody } from "./StickDeviceBody"

interface Sim3DStickProps {
  screenCanvas: HTMLCanvasElement | null
  onPressBtn0: () => void
  onPressBtn1: () => void
}

const noopSubscribe = () => () => {}
function useIsClient() {
  return useSyncExternalStore(noopSubscribe, () => true, () => false)
}

/**
 * 3D simulator for the M5StickC Plus2. Renders only on the client (r3f
 * cannot SSR). Props are the offscreen 240×135 canvas the body samples as
 * a CanvasTexture, and two button-press callbacks.
 *
 * Ported from ~/code/resident-web/app/components/try-it-now/Sim3DStick.tsx.
 * The original used Tailwind utility classes; rewritten here with inline
 * styles to avoid pulling in Tailwind for this single component.
 */
export function Sim3DStick({ screenCanvas, onPressBtn0, onPressBtn1 }: Sim3DStickProps) {
  const groupRef = useRef<THREE.Group>(null)
  const isClient = useIsClient()

  if (!isClient) {
    return <div style={{ width: "100%", aspectRatio: "2 / 1", background: "rgba(0,0,0,.1)" }} />
  }

  // Layout slot is 2:1. Inside it, the WebGL canvas is oversized
  // (-70% inset on each side ⇒ 240% × 240% of the slot) so a rotated
  // stick can extend past the slot's edges without being clipped — the
  // empty area of the canvas is transparent.
  return (
    <div style={{ width: "100%", aspectRatio: "2 / 1", position: "relative" }}>
      <div style={{ position: "absolute", inset: "-70%" }}>
        <Canvas
          orthographic
          camera={{ position: [0, 0, 5], zoom: 117, near: 0.1, far: 100 }}
          style={{ touchAction: "none", width: "100%", height: "100%" }}
          gl={{ alpha: true, antialias: true }}
        >
          <RotationHandler groupRef={groupRef} />
          <StickDeviceBody
            groupRef={groupRef}
            screenCanvas={screenCanvas}
            onPressBtnA={onPressBtn0}
            onPressBtnB={onPressBtn1}
          />
        </Canvas>
      </div>
    </div>
  )
}
