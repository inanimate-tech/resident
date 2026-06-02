import { useCallback, useEffect, useRef, useState } from "react"
import {
  compileLua,
  SCREEN_W, SCREEN_H,
  type LuaCtx, type LuaEvent, type StickRuntime,
} from "../lib/lua-runtime"

export { SCREEN_W, SCREEN_H }

interface Options {
  /** Lua source. When this changes, the previous runtime is torn down and rebuilt. */
  code: string | null
}

interface Result {
  /** Attach to the offscreen canvas the runtime paints into. */
  setCanvasRef: (el: HTMLCanvasElement | null) => void
  /** Fire a button event into the runtime. */
  handleTrigger: (buttonIndex: number) => void
  /** Last reported runtime error (cleared when a new code prop arrives). */
  error: string | null
}

const FPS = 10

/**
 * In-browser m5stick — compile/run Lua via Fengari, paint a 240×135 canvas at
 * 10 FPS, deliver button events. Owns its own ticker; consumers just attach a
 * canvas and call `handleTrigger`.
 */
export function useStickDevice({ code }: Options): Result {
  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const runtimeRef = useRef<StickRuntime | null>(null)
  const buttonCountRef = useRef(0)
  const startTimeRef = useRef(0)
  const [error, setError] = useState<string | null>(null)

  const setCanvasRef = useCallback((el: HTMLCanvasElement | null) => {
    canvasRef.current = el
  }, [])

  const makeCtx = useCallback((): LuaCtx => {
    const now = new Date()
    return {
      time_ms: Date.now() - startTimeRef.current,
      trigger_count: buttonCountRef.current,
      utc_h: now.getUTCHours(),
      utc_m: now.getUTCMinutes(),
      localtime_h: now.getHours(),
      localtime_m: now.getMinutes(),
      day_id: Math.floor(Date.now() / 86_400_000),
    }
  }, [])

  const paint = useCallback(() => {
    const canvas = canvasRef.current
    const runtime = runtimeRef.current
    if (!canvas || !runtime) return
    const ctx2d = canvas.getContext("2d")
    if (!ctx2d) return
    const src = runtime.getScreenBuffer()
    const img = ctx2d.createImageData(SCREEN_W, SCREEN_H)
    const data = img.data
    for (let i = 0, j = 0; i < SCREEN_W * SCREEN_H; i++, j += 3) {
      const o = i * 4
      data[o] = src[j]
      data[o + 1] = src[j + 1]
      data[o + 2] = src[j + 2]
      data[o + 3] = 255
    }
    ctx2d.putImageData(img, 0, 0)
  }, [])

  // Rebuild runtime whenever `code` changes; null code → tear down.
  useEffect(() => {
    setError(null)
    if (!code) {
      if (runtimeRef.current) { runtimeRef.current.destroy(); runtimeRef.current = null }
      return
    }
    let cancelled = false
    let raf = 0
    let lastTick = 0
    let runtime: StickRuntime | null = null
    const frameInterval = 1000 / FPS
    const tick = (now: number) => {
      if (cancelled) return
      const r = runtimeRef.current
      if (!r) return
      if (now - lastTick >= frameInterval) {
        const dt = lastTick === 0 ? 0 : now - lastTick
        lastTick = now
        if (r.hasFunction("on_tick")) {
          const err = r.callTick(makeCtx(), dt | 0)
          if (err) { setError(err); cancelled = true; return }
        }
        paint()
      }
      raf = requestAnimationFrame(tick)
    }

    ;(async () => {
      try {
        runtime = await compileLua(code)
      } catch (e) {
        if (!cancelled) setError(e instanceof Error ? e.message : String(e))
        return
      }
      if (cancelled) { runtime.destroy(); return }
      runtimeRef.current = runtime
      startTimeRef.current = Date.now()
      const initErr = runtime.callInit(makeCtx())
      if (initErr) {
        setError(initErr)
        return
      }
      paint()
      raf = requestAnimationFrame(tick)
    })()

    return () => {
      cancelled = true
      cancelAnimationFrame(raf)
      if (runtime) runtime.destroy()
      if (runtimeRef.current === runtime) runtimeRef.current = null
    }
  }, [code, paint, makeCtx])

  const handleTrigger = useCallback((buttonIndex: number) => {
    const r = runtimeRef.current
    if (!r) return
    buttonCountRef.current = (buttonCountRef.current + 1) % 256
    const event: LuaEvent = {
      name: "button",
      index: buttonIndex,
      count: buttonCountRef.current,
      ts_ms: Date.now(),
    }
    if (r.hasFunction("on_event")) {
      const err = r.callEvent(makeCtx(), event)
      if (err) setError(err)
    }
  }, [makeCtx])

  return { setCanvasRef, handleTrigger, error }
}
