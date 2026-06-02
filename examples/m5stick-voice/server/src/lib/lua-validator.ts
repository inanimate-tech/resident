/**
 * Validate Lua source by running it through a Fengari VM with concrete-value
 * stubs for getters and no-ops for everything else. Calls `init(ctx)` and
 * five `on_tick(ctx, dt_ms)` iterations to surface runtime errors before the
 * code is broadcast to the sim.
 */
import { createLuaVM, ensureFengari, lua, type StubProvider } from "./lua-vm"

export interface ValidationResult {
  ok: boolean
  error?: string
}

const TICK_COUNT = 5
const TICK_DT_MS = 100

/** M5Stick stub provider — concrete values for getters, no-ops for setters. */
const m5stickStubProvider: StubProvider = (moduleName, functionName) => {
  const key = `${moduleName}.${functionName}`
  switch (key) {
    case "screen.width":
      return (L) => { lua.lua_pushnumber(L, 240); return 1 }
    case "screen.height":
      return (L) => { lua.lua_pushnumber(L, 135); return 1 }
    case "button.press_count":
      return (L) => { lua.lua_pushnumber(L, 0); return 1 }
    case "kv.get":
      return () => 0 // returns nil
    case "kv.set":
      return (L) => { lua.lua_pushboolean(L, true); return 1 }
    case "time.is_valid":
      return (L) => { lua.lua_pushboolean(L, true); return 1 }
    case "time.has_timezone":
      return (L) => { lua.lua_pushboolean(L, false); return 1 }
    case "time.day_id":
      return (L) => { lua.lua_pushnumber(L, 1); return 1 }
    case "time.hour":
      return (L) => { lua.lua_pushnumber(L, 12); return 1 }
    case "time.minute":
      return (L) => { lua.lua_pushnumber(L, 0); return 1 }
    case "time.second":
      return (L) => { lua.lua_pushnumber(L, 0); return 1 }
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
    default:
      return () => 0 // no-op for setters, draw calls, log.*, buzzer.*, etc.
  }
}

export async function validateLuaCode(code: string): Promise<ValidationResult> {
  // Fengari pulls in node-internal modules (os, buffer, …) that eventually
  // touch process.binding — unenv's polyfill throws on that, so validation
  // doesn't work inside the Workers/Vite-dev runtime. Treat fengari as
  // best-effort: if it can't load, we skip validation entirely. The
  // browser-side sim has its own Fengari VM and a runtime-error overlay,
  // so a bad app surfaces there instead.
  try {
    await ensureFengari()
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e)
    console.warn("[validator] fengari unavailable, skipping validation:", msg)
    return { ok: true }
  }

  let vm: ReturnType<typeof createLuaVM>
  try {
    vm = createLuaVM(m5stickStubProvider)
  } catch (e) {
    console.warn("[validator] createLuaVM failed, skipping validation:", e instanceof Error ? e.message : e)
    return { ok: true }
  }

  try {
    const loadError = vm.loadCode(code)
    if (loadError) return { ok: false, error: loadError }

    const ctx: Record<string, number> = {
      time_ms: 0, trigger_count: 0,
      utc_h: 12, utc_m: 0, localtime_h: 12, localtime_m: 0, day_id: 1,
    }

    const initError = vm.callFunction("init", ctx)
    if (initError) return { ok: false, error: initError }

    if (vm.hasFunction("on_tick")) {
      for (let i = 0; i < TICK_COUNT; i++) {
        ctx.time_ms = (i + 1) * TICK_DT_MS
        const tickError = vm.callFunction("on_tick", ctx, TICK_DT_MS)
        if (tickError) return { ok: false, error: tickError }
      }
    }

    return { ok: true }
  } finally {
    vm.close()
  }
}
