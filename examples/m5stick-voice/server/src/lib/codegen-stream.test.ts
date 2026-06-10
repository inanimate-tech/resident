import { describe, expect, it } from "vitest"
import { parseSSELine } from "./codegen-stream"

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
