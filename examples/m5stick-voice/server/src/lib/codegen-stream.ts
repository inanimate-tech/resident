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

/** Number of completed (newline-terminated) lines in `text`. */
export function countLines(text: string): number {
  let n = 0
  for (let i = 0; i < text.length; i++) if (text[i] === "\n") n++
  return n
}

export interface LineProgress {
  /** Feed the full accumulated text so far. */
  update(accumulated: string): void
  /** Emit the final count if it has not been emitted yet. */
  flush(): void
}

/**
 * Emits the completed-line count of streamed text, throttled to at most one
 * emission per `intervalMs`, only when the count changes. `now` is injectable
 * for tests; defaults to `Date.now`.
 */
export function createLineProgress(
  emit: (lines: number) => void,
  opts: { intervalMs?: number; now?: () => number } = {},
): LineProgress {
  const intervalMs = opts.intervalMs ?? 250
  const now = opts.now ?? Date.now
  let lastEmitAt = 0
  let emittedLines = -1
  let latestLines = 0
  return {
    update(accumulated) {
      latestLines = countLines(accumulated)
      const t = now()
      if (latestLines !== emittedLines && t - lastEmitAt >= intervalMs) {
        emittedLines = latestLines
        lastEmitAt = t
        emit(latestLines)
      }
    },
    flush() {
      if (latestLines !== emittedLines) {
        emittedLines = latestLines
        lastEmitAt = now()
        emit(latestLines)
      }
    },
  }
}
