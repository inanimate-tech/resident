import { describe, expect, it } from "vitest"
import { parseSSELine } from "./codegen-stream"
import { countLines, createLineProgress } from "./codegen-stream"

describe("parseSSELine", () => {
  it("extracts delta content from a data line", () => {
    const line = `data: ${JSON.stringify({ choices: [{ delta: { content: "hi" } }] })}`
    expect(parseSSELine(line)).toEqual({ content: "hi" })
  })

  it("flags the [DONE] sentinel", () => {
    expect(parseSSELine("data: [DONE]")).toEqual({ done: true })
  })

  it("ignores blank lines and non-data lines", () => {
    expect(parseSSELine("")).toEqual({})
    expect(parseSSELine(": keep-alive")).toEqual({})
  })

  it("ignores data lines with no content delta (e.g. role-only chunk)", () => {
    const line = `data: ${JSON.stringify({ choices: [{ delta: { role: "assistant" } }] })}`
    expect(parseSSELine(line)).toEqual({})
  })

  it("returns empty on malformed JSON rather than throwing", () => {
    expect(parseSSELine("data: {not json")).toEqual({})
  })
})

describe("countLines", () => {
  it("counts completed (newline-terminated) lines", () => {
    expect(countLines("")).toBe(0)
    expect(countLines("one line, no newline")).toBe(0)
    expect(countLines("a\nb\n")).toBe(2)
    expect(countLines("a\nb")).toBe(1)
  })
})

describe("createLineProgress", () => {
  it("emits only when the line count changes and the interval has elapsed", () => {
    let clock = 0
    const emitted: number[] = []
    const p = createLineProgress((n) => emitted.push(n), { intervalMs: 250, now: () => clock })

    p.update("a\n")          // t=0: first change, 0>=250? no -> suppressed
    clock = 100
    p.update("a\nb\n")       // t=100: still < 250 -> suppressed
    clock = 300
    p.update("a\nb\nc\n")    // t=300: >=250 and changed -> emit 3
    clock = 350
    p.update("a\nb\nc\n")    // no change -> nothing
    expect(emitted).toEqual([3])
  })

  it("flush emits the final count if it was never emitted", () => {
    let clock = 0
    const emitted: number[] = []
    const p = createLineProgress((n) => emitted.push(n), { intervalMs: 250, now: () => clock })
    p.update("a\nb\n")       // suppressed (t=0)
    p.flush()                // emits 2
    expect(emitted).toEqual([2])
  })

  it("flush is a no-op when the latest count was already emitted", () => {
    let clock = 1000
    const emitted: number[] = []
    const p = createLineProgress((n) => emitted.push(n), { intervalMs: 250, now: () => clock })
    p.update("a\nb\n")       // t=1000, elapsed since 0 -> emit 2
    p.flush()                // already emitted 2 -> no-op
    expect(emitted).toEqual([2])
  })
})
