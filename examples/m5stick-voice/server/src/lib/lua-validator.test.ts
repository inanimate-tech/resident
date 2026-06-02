import { describe, expect, it } from "vitest"
import { validateLuaCode } from "./lua-validator"

describe("validateLuaCode", () => {
  it("accepts a valid m5stick app", () => {
    const code = `
      function init(ctx)
        screen.clear()
        screen.text(10, 10, "HI")
        screen.flip()
      end
      function on_tick(ctx, dt_ms)
        screen.clear()
        screen.flip()
      end
    `
    const result = validateLuaCode(code)
    expect(result.ok).toBe(true)
    expect(result.error).toBeUndefined()
  })

  it("rejects code that calls a nonexistent screen method", () => {
    const code = `function init(ctx) screen.nope() end`
    const result = validateLuaCode(code)
    expect(result.ok).toBe(false)
    expect(result.error).toBeTruthy()
  })

  it("rejects code that fails to compile", () => {
    const code = `function init(ctx) if then end`
    const result = validateLuaCode(code)
    expect(result.ok).toBe(false)
    expect(result.error).toBeTruthy()
  })

  it("runs on_tick five times against the stub set without crashing", () => {
    const code = `
      local counter = 0
      function init(ctx) counter = 0 end
      function on_tick(ctx, dt_ms)
        counter = counter + 1
        local ax, ay, az = imu.accel()
        screen.clear()
        screen.fill_rect(0, 0, screen.width(), screen.height(), 0, 0, 255)
        screen.flip()
      end
    `
    const result = validateLuaCode(code)
    expect(result.ok).toBe(true)
  })

  it("accepts apps that only define init", () => {
    const code = `function init(ctx) screen.clear() screen.flip() end`
    const result = validateLuaCode(code)
    expect(result.ok).toBe(true)
  })
})
