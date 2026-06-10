/** Parse one line of an OpenAI chat-completions SSE stream. */
export function parseSSELine(line: string): { content?: string; done?: boolean } {
  const trimmed = line.trim()
  if (!trimmed.startsWith("data:")) return {}
  const payload = trimmed.slice(5).trim()
  if (payload === "[DONE]") return { done: true }
  try {
    const json = JSON.parse(payload) as {
      choices?: Array<{ delta?: { content?: unknown } }>
    }
    const content = json.choices?.[0]?.delta?.content
    return typeof content === "string" ? { content } : {}
  } catch {
    return {}
  }
}
