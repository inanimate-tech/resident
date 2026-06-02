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

// @ts-expect-error -- fengari is CJS-only with no type declarations
import fengari from "fengari"

const { lua, lauxlib, lualib } = fengari
const to_luastring: (s: string) => unknown = fengari.to_luastring

import {
  M5STICK_MODULES,
  GLOBAL_HELPERS,
  BARE_MATH_FUNCTIONS,
  type DeviceApiModule,
} from "./device-apis"

export { lua, lauxlib, lualib, to_luastring }

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
  /**
   * Call a global Lua function with `ctx` (and optional extra args). Returns
   * null on success, error string on failure, or null no-op if the function
   * is undefined.
   */
  callFunction(name: string, ...args: unknown[]): string | null
  /** True if a global function with this name exists. */
  hasFunction(name: string): boolean
  /** Direct access to the Lua state (for advanced stub implementations). */
  readonly L: unknown
  /** Close the VM and free resources. */
  close(): void
}

/** Push a JS value onto the Lua stack as the appropriate Lua type. */
export function pushValue(L: unknown, value: unknown): void {
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

/** Pop the error message off the top of the Lua stack. */
export function getErrorMessage(L: unknown): string {
  const msg = lua.lua_tojsstring(L, -1)
  lua.lua_pop(L, 1)
  return msg ?? "unknown error"
}

export function createLuaVM(stubProvider: StubProvider): LuaVM {
  const L = lauxlib.luaL_newstate()
  lualib.luaL_openlibs(L)

  // Register M5StickC modules.
  for (const mod of M5STICK_MODULES) {
    registerModule(L, mod, stubProvider)
  }

  // Register global helpers (rgb, fract, beat, noise2d).
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
      if (loadResult !== 0) return getErrorMessage(L)
      const execResult = lua.lua_pcall(L, 0, 0, 0)
      if (execResult !== 0) return getErrorMessage(L)
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
      if (result !== 0) return getErrorMessage(L)
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

function registerModule(
  L: unknown,
  mod: DeviceApiModule,
  stubProvider: StubProvider,
): void {
  lua.lua_createtable(L, 0, mod.functions.length)
  for (const fn of mod.functions) {
    const impl = stubProvider(mod.name, fn, mod.config)
    lua.lua_pushjsfunction(L, impl)
    lua.lua_setfield(L, -2, to_luastring(fn))
  }
  lua.lua_setglobal(L, to_luastring(mod.name))
}

function registerGlobalHelpers(L: unknown): void {
  for (const name of GLOBAL_HELPERS) {
    switch (name) {
      case "rgb":
        lua.lua_pushjsfunction(L, (Li: unknown) => {
          const r = lua.lua_tonumber(Li, 1)
          const g = lua.lua_tonumber(Li, 2)
          const b = lua.lua_tonumber(Li, 3)
          // sandbox.md says rgb takes 0-1 floats and returns a packed colour
          // via a negative-int sentinel. We pack the same way here.
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
        // Deterministic value noise placeholder — returns 0. The validator
        // doesn't care; the in-browser sim can override this if needed.
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
