/**
 * Fengari Lua VM setup for the m5stick sandbox.
 *
 * Creates a Lua 5.3 state with M5StickC modules + global helpers stubbed by
 * a caller-supplied `StubProvider`. Same shape as hawthorn's lua-vm.ts,
 * narrowed to one device type.
 *
 * Used by:
 * - lua-validator.ts (server-side validation — no-op stubs)
 * - lua-runtime.ts   (browser sim — state-tracking stubs)
 */

import {
  M5STICK_MODULES,
  GLOBAL_HELPERS,
  BARE_MATH_FUNCTIONS,
  type DeviceApiModule,
} from "./device-apis"

// Why dynamic import: a top-level `import fengari from "fengari"` evaluates at
// module load, and fengari's CJS init hits process.binding via unenv's
// polyfill. That fires during @cloudflare/vite-plugin's worker-entry analysis
// (`npm run dev` boot fails) AND the Workers runtime itself has no `require`
// (so a synchronous require() in a function trips at runtime). Dynamic import
// avoids both: the analyser skips it, and Workers + vitest both honour it.
interface FengariBindings {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  lua: any
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  lauxlib: any
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  lualib: any
  to_luastring: (s: string) => unknown
}
let _fengari: FengariBindings | null = null
let _loading: Promise<FengariBindings> | null = null

/** Load fengari once. Idempotent and concurrency-safe. */
export async function ensureFengari(): Promise<FengariBindings> {
  if (_fengari) return _fengari
  if (_loading) return _loading
  _loading = (async () => {
    const mod = await import("fengari-web")
    // CJS-via-ESM: bindings may live on the namespace or under `.default`.
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const src: any = (mod as any).default ?? mod
    _fengari = {
      lua: src.lua,
      lauxlib: src.lauxlib,
      lualib: src.lualib,
      to_luastring: src.to_luastring,
    }
    return _fengari
  })()
  return _loading
}

function fg(): FengariBindings {
  if (!_fengari) {
    throw new Error("fengari not loaded — call await ensureFengari() before createLuaVM()")
  }
  return _fengari
}

/**
 * `lua` is exported so callers (e.g. validator stubs) can call `lua.lua_pushnumber`
 * inside Fengari C-API functions. Safe to use only after createLuaVM has run
 * (which is guaranteed in any stub invoked from the VM).
 */
// eslint-disable-next-line @typescript-eslint/no-explicit-any
export const lua: any = new Proxy({}, {
  get(_t, prop: string) { return fg().lua[prop] },
})

/** Fengari C-API function: reads args from stack, pushes returns, returns count. */
export type FengariFunction = (L: unknown) => number

/**
 * Called for each module.function to get the C-API implementation.
 * The consumer decides what each stub does (no-op vs state-tracking).
 */
export type StubProvider = (
  moduleName: string,
  functionName: string,
  config?: Record<string, number>,
) => FengariFunction

export interface LuaVM {
  /** Load and execute Lua source. Returns null on success, error string on failure. */
  loadCode(code: string): string | null
  /** Call a global Lua function. Returns null on success, error string on failure. */
  callFunction(name: string, ...args: unknown[]): string | null
  /** True if a global function with this name exists. */
  hasFunction(name: string): boolean
  /** Direct access to the Lua state. */
  readonly L: unknown
  /** Close the VM and free resources. */
  close(): void
}

export function createLuaVM(stubProvider: StubProvider): LuaVM {
  const { lua, lauxlib, lualib, to_luastring } = fg()
  const L = lauxlib.luaL_newstate()
  lualib.luaL_openlibs(L)

  for (const mod of M5STICK_MODULES) registerModule(L, mod, stubProvider)
  registerGlobalHelpers(L)

  // Re-bind bare math functions as globals (no `math.` prefix).
  for (const name of BARE_MATH_FUNCTIONS) {
    lua.lua_getglobal(L, to_luastring("math"))
    lua.lua_getfield(L, -1, to_luastring(name))
    lua.lua_setglobal(L, to_luastring(name))
    lua.lua_pop(L, 1)
  }

  return {
    loadCode(code: string): string | null {
      const loadResult = lauxlib.luaL_loadstring(L, to_luastring(code))
      if (loadResult !== 0) return readError(L)
      const execResult = lua.lua_pcall(L, 0, 0, 0)
      if (execResult !== 0) return readError(L)
      return null
    },
    callFunction(name: string, ...args: unknown[]): string | null {
      lua.lua_getglobal(L, to_luastring(name))
      if (!lua.lua_isfunction(L, -1)) {
        lua.lua_pop(L, 1)
        return null
      }
      for (const arg of args) pushValue(L, arg)
      const result = lua.lua_pcall(L, args.length, 0, 0)
      if (result !== 0) return readError(L)
      return null
    },
    hasFunction(name: string): boolean {
      lua.lua_getglobal(L, to_luastring(name))
      const isFunc = lua.lua_isfunction(L, -1)
      lua.lua_pop(L, 1)
      return isFunc
    },
    get L() { return L },
    close() { lua.lua_close(L) },
  }
}

function pushValue(L: unknown, value: unknown): void {
  const { lua, to_luastring } = fg()
  if (value === null || value === undefined) {
    lua.lua_pushnil(L)
  } else if (typeof value === "number") {
    if (Number.isInteger(value)) lua.lua_pushinteger(L, value)
    else lua.lua_pushnumber(L, value)
  } else if (typeof value === "string") {
    lua.lua_pushstring(L, to_luastring(value))
  } else if (typeof value === "boolean") {
    lua.lua_pushboolean(L, value)
  } else if (typeof value === "object") {
    const obj = value as Record<string, unknown>
    const keys = Object.keys(obj)
    lua.lua_createtable(L, 0, keys.length)
    for (const key of keys) {
      pushValue(L, obj[key])
      lua.lua_setfield(L, -2, to_luastring(key))
    }
  }
}

function readError(L: unknown): string {
  const { lua } = fg()
  const msg = lua.lua_tojsstring(L, -1)
  lua.lua_pop(L, 1)
  return msg ?? "unknown error"
}

function registerModule(
  L: unknown,
  mod: DeviceApiModule,
  stubProvider: StubProvider,
): void {
  const { lua, to_luastring } = fg()
  lua.lua_createtable(L, 0, mod.functions.length)
  for (const fn of mod.functions) {
    const impl = stubProvider(mod.name, fn, mod.config)
    lua.lua_pushjsfunction(L, impl)
    lua.lua_setfield(L, -2, to_luastring(fn))
  }
  lua.lua_setglobal(L, to_luastring(mod.name))
}

function registerGlobalHelpers(L: unknown): void {
  const { lua, to_luastring } = fg()
  for (const name of GLOBAL_HELPERS) {
    switch (name) {
      case "rgb":
        lua.lua_pushjsfunction(L, (Li: unknown) => {
          const r = lua.lua_tonumber(Li, 1)
          const g = lua.lua_tonumber(Li, 2)
          const b = lua.lua_tonumber(Li, 3)
          // sandbox.md says rgb takes 0-1 floats and returns a packed colour
          // via a negative-int sentinel.
          const value =
            ((Math.floor(r * 255) & 0xff) << 16) |
            ((Math.floor(g * 255) & 0xff) << 8) |
            (Math.floor(b * 255) & 0xff)
          lua.lua_pushnumber(Li, -value)
          return 1
        })
        break
      case "fract":
        lua.lua_pushjsfunction(L, (Li: unknown) => {
          const x = lua.lua_tonumber(Li, 1)
          lua.lua_pushnumber(Li, x - Math.floor(x))
          return 1
        })
        break
      case "beat":
        lua.lua_pushjsfunction(L, (Li: unknown) => {
          const bpm = lua.lua_tonumber(Li, 1) || 60
          const t = lua.lua_tonumber(Li, 2) || 0
          lua.lua_pushnumber(Li, t / (60000 / bpm))
          return 1
        })
        break
      case "noise2d":
        lua.lua_pushjsfunction(L, (Li: unknown) => {
          void lua.lua_tonumber(Li, 1)
          void lua.lua_tonumber(Li, 2)
          lua.lua_pushnumber(Li, 0)
          return 1
        })
        break
    }
    lua.lua_setglobal(L, to_luastring(name))
  }
}
