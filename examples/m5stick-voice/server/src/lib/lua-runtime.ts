/**
 * In-browser m5stick Lua runtime. Wraps the Fengari VM with state-tracking
 * stubs that draw into a 240×135 RGB framebuffer, accumulate button presses,
 * and stub the rest of the m5stick sandbox surface. The consumer (a React
 * component / hook) calls init/tick/event in its render loop and uses
 * getScreenBuffer() to repaint a canvas.
 */
import { createLuaVM, ensureFengari, lua, type StubProvider } from "./lua-vm"

export const SCREEN_W = 240
export const SCREEN_H = 135

export interface LuaCtx {
  time_ms: number
  trigger_count: number
  utc_h: number
  utc_m: number
  localtime_h: number
  localtime_m: number
  day_id: number
}

export interface LuaEvent {
  name: string
  index?: number
  count?: number
  ts_ms?: number
  data?: Record<string, unknown>
  from?: string
}

export interface StickRuntime {
  callInit(ctx: LuaCtx): string | null
  callTick(ctx: LuaCtx, dt_ms: number): string | null
  callEvent(ctx: LuaCtx, event: LuaEvent): string | null
  hasFunction(name: string): boolean
  /** Read-only view of the current framebuffer (RGB, row-major). */
  getScreenBuffer(): Uint8Array
  destroy(): void
}

/**
 * Compile and prepare a Lua app for the m5stick. The returned runtime can
 * be driven from a rAF loop; init/tick/event return null on success or an
 * error string on failure (mirrors the validator's contract).
 */
export async function compileLua(code: string): Promise<StickRuntime> {
  await ensureFengari()
  // The framebuffer is the runtime's primary state — closed over by the
  // screen.* stubs below. RGB packed; alpha is implied 255.
  const screen = new Uint8Array(SCREEN_W * SCREEN_H * 3)
  let dirty = false // tracks whether `screen.clear/draw/...` happened since last flip
  let buttonCount = 0

  const stubProvider: StubProvider = (moduleName, functionName) => {
    const key = `${moduleName}.${functionName}`
    switch (key) {
      case "screen.width":
        return (L) => { lua.lua_pushnumber(L, SCREEN_W); return 1 }
      case "screen.height":
        return (L) => { lua.lua_pushnumber(L, SCREEN_H); return 1 }
      case "screen.clear":
        return (L) => {
          const top = lua.lua_gettop(L)
          let r = 0, g = 0, b = 0
          if (top >= 3) {
            r = lua.lua_tonumber(L, 1) | 0
            g = lua.lua_tonumber(L, 2) | 0
            b = lua.lua_tonumber(L, 3) | 0
          }
          for (let i = 0; i < screen.length; i += 3) {
            screen[i] = r; screen[i + 1] = g; screen[i + 2] = b
          }
          dirty = true
          return 0
        }
      case "screen.pixel":
        return (L) => {
          const x = lua.lua_tonumber(L, 1) | 0
          const y = lua.lua_tonumber(L, 2) | 0
          const r = lua.lua_tonumber(L, 3) | 0
          const g = lua.lua_tonumber(L, 4) | 0
          const b = lua.lua_tonumber(L, 5) | 0
          setPixel(screen, x, y, r, g, b)
          dirty = true
          return 0
        }
      case "screen.fill_rect":
        return (L) => {
          const x = lua.lua_tonumber(L, 1) | 0
          const y = lua.lua_tonumber(L, 2) | 0
          const w = lua.lua_tonumber(L, 3) | 0
          const h = lua.lua_tonumber(L, 4) | 0
          const r = lua.lua_tonumber(L, 5) | 0
          const g = lua.lua_tonumber(L, 6) | 0
          const b = lua.lua_tonumber(L, 7) | 0
          fillRect(screen, x, y, w, h, r, g, b)
          dirty = true
          return 0
        }
      case "screen.rect":
        return (L) => {
          const x = lua.lua_tonumber(L, 1) | 0
          const y = lua.lua_tonumber(L, 2) | 0
          const w = lua.lua_tonumber(L, 3) | 0
          const h = lua.lua_tonumber(L, 4) | 0
          const r = lua.lua_tonumber(L, 5) | 0
          const g = lua.lua_tonumber(L, 6) | 0
          const b = lua.lua_tonumber(L, 7) | 0
          strokeRect(screen, x, y, w, h, r, g, b)
          dirty = true
          return 0
        }
      case "screen.line":
        return (L) => {
          const x0 = lua.lua_tonumber(L, 1) | 0
          const y0 = lua.lua_tonumber(L, 2) | 0
          const x1 = lua.lua_tonumber(L, 3) | 0
          const y1 = lua.lua_tonumber(L, 4) | 0
          const r = lua.lua_tonumber(L, 5) | 0
          const g = lua.lua_tonumber(L, 6) | 0
          const b = lua.lua_tonumber(L, 7) | 0
          drawLine(screen, x0, y0, x1, y1, r, g, b)
          dirty = true
          return 0
        }
      case "screen.text":
        return (L) => {
          const x = lua.lua_tonumber(L, 1) | 0
          const y = lua.lua_tonumber(L, 2) | 0
          const str = lua.lua_tojsstring(L, 3) ?? ""
          const top = lua.lua_gettop(L)
          let size = 2, r = 255, g = 255, b = 255
          if (top >= 4) size = lua.lua_tonumber(L, 4) | 0
          if (top >= 7) {
            r = lua.lua_tonumber(L, 5) | 0
            g = lua.lua_tonumber(L, 6) | 0
            b = lua.lua_tonumber(L, 7) | 0
          }
          drawText(screen, x, y, str, size, r, g, b)
          dirty = true
          return 0
        }
      case "screen.flip":
        return () => { dirty = false; return 0 }
      case "button.press_count":
        return (L) => { lua.lua_pushnumber(L, buttonCount); return 1 }
      case "imu.accel":
        return (L) => {
          lua.lua_pushnumber(L, 0); lua.lua_pushnumber(L, 0); lua.lua_pushnumber(L, 1)
          return 3
        }
      case "imu.gyro":
        return (L) => {
          lua.lua_pushnumber(L, 0); lua.lua_pushnumber(L, 0); lua.lua_pushnumber(L, 0)
          return 3
        }
      case "imu.temp":
        return (L) => { lua.lua_pushnumber(L, 0); return 1 }
      case "kv.get":
        return () => 0
      case "kv.set":
        return (L) => { lua.lua_pushboolean(L, true); return 1 }
      case "time.is_valid":
        return (L) => { lua.lua_pushboolean(L, true); return 1 }
      case "time.has_timezone":
        return (L) => { lua.lua_pushboolean(L, false); return 1 }
      case "time.day_id":
        return (L) => { lua.lua_pushnumber(L, 1); return 1 }
      case "time.hour":
        return (L) => { lua.lua_pushnumber(L, new Date().getHours()); return 1 }
      case "time.minute":
        return (L) => { lua.lua_pushnumber(L, new Date().getMinutes()); return 1 }
      case "time.second":
        return (L) => { lua.lua_pushnumber(L, new Date().getSeconds()); return 1 }
      default:
        return () => 0
    }
  }

  const vm = createLuaVM(stubProvider)
  const loadError = vm.loadCode(code)
  if (loadError) {
    vm.close()
    throw new Error(`compile error: ${loadError}`)
  }

  return {
    callInit(ctx: LuaCtx): string | null {
      return vm.callFunction("init", ctx)
    },
    callTick(ctx: LuaCtx, dt_ms: number): string | null {
      return vm.callFunction("on_tick", ctx, dt_ms)
    },
    callEvent(ctx: LuaCtx, event: LuaEvent): string | null {
      // expose to stubs (e.g., button.press_count) so they can read fresh
      if (event.name === "button" && typeof event.count === "number") {
        buttonCount = event.count
      }
      return vm.callFunction("on_event", ctx, event as unknown as Record<string, unknown>)
    },
    hasFunction: (name: string) => vm.hasFunction(name),
    getScreenBuffer: () => screen,
    destroy: () => vm.close(),
  }
  // (suppress 'dirty' unused warning — kept for future double-buffering)
  void dirty
}

// ---- minimal software rasteriser ----

function setPixel(buf: Uint8Array, x: number, y: number, r: number, g: number, b: number): void {
  if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return
  const i = (y * SCREEN_W + x) * 3
  buf[i] = r; buf[i + 1] = g; buf[i + 2] = b
}

function fillRect(buf: Uint8Array, x: number, y: number, w: number, h: number, r: number, g: number, b: number): void {
  const x0 = Math.max(0, x), y0 = Math.max(0, y)
  const x1 = Math.min(SCREEN_W, x + w), y1 = Math.min(SCREEN_H, y + h)
  for (let yy = y0; yy < y1; yy++) {
    for (let xx = x0; xx < x1; xx++) {
      const i = (yy * SCREEN_W + xx) * 3
      buf[i] = r; buf[i + 1] = g; buf[i + 2] = b
    }
  }
}

function strokeRect(buf: Uint8Array, x: number, y: number, w: number, h: number, r: number, g: number, b: number): void {
  for (let xx = x; xx < x + w; xx++) { setPixel(buf, xx, y, r, g, b); setPixel(buf, xx, y + h - 1, r, g, b) }
  for (let yy = y; yy < y + h; yy++) { setPixel(buf, x, yy, r, g, b); setPixel(buf, x + w - 1, yy, r, g, b) }
}

function drawLine(buf: Uint8Array, x0: number, y0: number, x1: number, y1: number, r: number, g: number, b: number): void {
  // Bresenham
  let dx = Math.abs(x1 - x0), sx = x0 < x1 ? 1 : -1
  let dy = -Math.abs(y1 - y0), sy = y0 < y1 ? 1 : -1
  let err = dx + dy
  let x = x0, y = y0
  for (;;) {
    setPixel(buf, x, y, r, g, b)
    if (x === x1 && y === y1) break
    const e2 = 2 * err
    if (e2 >= dy) { err += dy; x += sx }
    if (e2 <= dx) { err += dx; y += sy }
  }
}

// 5×7 bitmap font for digits + uppercase. Tiny and deliberately incomplete —
// generated apps mostly draw shapes; text is a nice-to-have for clocks/counts.
// Each glyph is 5 bits wide × 7 rows tall, packed as 7 byte rows (low 5 bits).
const FONT_5x7: Record<string, number[]> = {
  " ": [0,0,0,0,0,0,0],
  "0": [0x0e,0x11,0x13,0x15,0x19,0x11,0x0e],
  "1": [0x04,0x0c,0x04,0x04,0x04,0x04,0x0e],
  "2": [0x0e,0x11,0x01,0x06,0x08,0x10,0x1f],
  "3": [0x0e,0x11,0x01,0x06,0x01,0x11,0x0e],
  "4": [0x02,0x06,0x0a,0x12,0x1f,0x02,0x02],
  "5": [0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e],
  "6": [0x06,0x08,0x10,0x1e,0x11,0x11,0x0e],
  "7": [0x1f,0x01,0x02,0x04,0x08,0x08,0x08],
  "8": [0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e],
  "9": [0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c],
  ":": [0x00,0x04,0x00,0x00,0x00,0x04,0x00],
  ".": [0x00,0x00,0x00,0x00,0x00,0x04,0x00],
  "-": [0x00,0x00,0x00,0x0e,0x00,0x00,0x00],
  "A": [0x0e,0x11,0x11,0x1f,0x11,0x11,0x11],
  "B": [0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e],
  "C": [0x0e,0x11,0x10,0x10,0x10,0x11,0x0e],
  "D": [0x1e,0x11,0x11,0x11,0x11,0x11,0x1e],
  "E": [0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f],
  "F": [0x1f,0x10,0x10,0x1e,0x10,0x10,0x10],
  "G": [0x0e,0x11,0x10,0x17,0x11,0x11,0x0e],
  "H": [0x11,0x11,0x11,0x1f,0x11,0x11,0x11],
  "I": [0x0e,0x04,0x04,0x04,0x04,0x04,0x0e],
  "J": [0x07,0x02,0x02,0x02,0x02,0x12,0x0c],
  "K": [0x11,0x12,0x14,0x18,0x14,0x12,0x11],
  "L": [0x10,0x10,0x10,0x10,0x10,0x10,0x1f],
  "M": [0x11,0x1b,0x15,0x15,0x11,0x11,0x11],
  "N": [0x11,0x11,0x19,0x15,0x13,0x11,0x11],
  "O": [0x0e,0x11,0x11,0x11,0x11,0x11,0x0e],
  "P": [0x1e,0x11,0x11,0x1e,0x10,0x10,0x10],
  "Q": [0x0e,0x11,0x11,0x11,0x15,0x12,0x0d],
  "R": [0x1e,0x11,0x11,0x1e,0x14,0x12,0x11],
  "S": [0x0e,0x11,0x10,0x0e,0x01,0x11,0x0e],
  "T": [0x1f,0x04,0x04,0x04,0x04,0x04,0x04],
  "U": [0x11,0x11,0x11,0x11,0x11,0x11,0x0e],
  "V": [0x11,0x11,0x11,0x11,0x11,0x0a,0x04],
  "W": [0x11,0x11,0x11,0x15,0x15,0x15,0x0a],
  "X": [0x11,0x11,0x0a,0x04,0x0a,0x11,0x11],
  "Y": [0x11,0x11,0x11,0x0a,0x04,0x04,0x04],
  "Z": [0x1f,0x01,0x02,0x04,0x08,0x10,0x1f],
}

function drawText(buf: Uint8Array, x: number, y: number, s: string, size: number, r: number, g: number, b: number): void {
  const upper = s.toUpperCase()
  let cx = x
  for (const ch of upper) {
    const glyph = FONT_5x7[ch] ?? FONT_5x7[" "]
    for (let row = 0; row < 7; row++) {
      for (let col = 0; col < 5; col++) {
        if ((glyph[row] >> (4 - col)) & 1) {
          fillRect(buf, cx + col * size, y + row * size, size, size, r, g, b)
        }
      }
    }
    cx += 6 * size
  }
}
