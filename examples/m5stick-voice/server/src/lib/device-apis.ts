/**
 * Lua API surface for the M5StickC Plus2 — narrowed from hawthorn's
 * multi-device registry. Mirrors `examples/m5stick-demo/DEVICE-SKILL.md`
 * and `tools/agent-plugin/skills/create-app/docs/sandbox.md`. Used by:
 * - lua-validator.ts (server-side validation with no-op stubs)
 * - lua-runtime.ts (browser sim with state-tracking stubs)
 */

export interface DeviceApiModule {
  /** Module name as exposed to Lua. */
  name: string
  /** Function names on this module. */
  functions: string[]
  /** Module-specific config (dimensions, counts, etc.). */
  config?: Record<string, number>
}

export const M5STICK_MODULES: DeviceApiModule[] = [
  {
    name: "screen",
    functions: [
      "clear", "text", "qr", "fill_rect", "rect", "line",
      "triangle", "fill_triangle", "pixel", "flip",
      "set_brightness", "width", "height",
    ],
    config: { width: 240, height: 135 },
  },
  { name: "imu", functions: ["accel", "gyro", "temp"] },
  { name: "buzzer", functions: ["beep", "tone", "stop"] },
  { name: "button", functions: ["press_count"], config: { count: 2 } },
  { name: "log", functions: ["info", "warn", "error"] },
  { name: "kv", functions: ["get", "set"] },
  {
    name: "time",
    functions: ["is_valid", "has_timezone", "hour", "minute", "second", "day_id"],
  },
]

/** Global helper functions registered without a module prefix. */
export const GLOBAL_HELPERS = ["rgb", "fract", "beat", "noise2d"] as const

/** Math functions registered as bare globals (no `math.` prefix). */
export const BARE_MATH_FUNCTIONS = [
  "floor", "ceil", "abs", "sin", "cos", "tan", "sqrt", "min", "max", "fmod",
] as const
