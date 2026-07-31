import { DeviceAgent, routeDeviceRequest } from "@inanimate/resident/cloudflare"
import type { Connection, WSMessage } from "agents"
import { generateObject, generateText } from "ai"
import { createGatewayProvider } from "workers-ai-provider/gateway"
import { createAnthropic } from "@ai-sdk/anthropic"
import { z } from "zod"
import luaparse from "luaparse"
import arcLua from "../../app/arc.lua"
import adventureLua from "../../app/adventure.lua"
import { viewerHtml } from "./viewer"

/**
 * The agent side of the arc loop (see spikes/arc/DESIGN.md):
 *
 *   reflex   — runs on the device (the shipped arc generation)
 *   response — arc:ask events land here and are answered with inference
 *   revision — POST /arc/push ships a (re)generation to the device
 *
 * Inference goes through Cloudflare AI Gateway with BYOK: the gateway holds
 * the Anthropic key, so this worker carries no model-provider secret.
 */

// Fast tier: story turns are latency-sensitive and narrow.
const ASK_MODEL = "claude-haiku-4-5-20251001"
// Code tier: remixes write Lua — rarer, worth a stronger model.
const REMIX_MODEL = "claude-sonnet-5"

// Must match the seed store in app/adventure.lua so the story history starts
// at the scene the device is actually showing.
const INTRO = {
  text: "You wake in a moonlit orchard. A lantern glows by the gate, and something rustles in the long grass.",
  a: "Take the lantern",
  b: "Follow the rustle",
}

// The device's incoming-event data buffer is 256 bytes of serialized JSON —
// the reply MUST fit or it is truncated mid-JSON and dropped by the app.
const REPLY_BUDGET = 240

const SceneSchema = z.object({
  text: z.string().describe(
    "The next story beat, second person, present tense, max 100 characters. Evocative, concrete, no choice text here."),
  a: z.string().describe("Label for choice A, max 16 characters, imperative"),
  b: z.string().describe("Label for choice B, max 16 characters, imperative"),
})

const STORY_SYSTEM = `You are the narrator of a tiny choose-your-own-adventure running on a 240x135 pixel badge. Gentle folk-tale strangeness; PG; keep momentum — every beat changes the situation. The reader carries a lantern; each turn includes "tilt" (-1..1), how the lantern is held — let where the light falls subtly shape what is revealed. HARD LIMITS: text <= 100 characters, each choice label <= 16 characters (labels are cut off past that, so end them cleanly). Never mention the limits or the tilt.`

const VIEW_SYSTEM = `You design the ENTIRE look of a tiny 240x135 pixel story device by writing a Lua chunk. Output ONLY Lua code — no markdown fences, no commentary. The chunk must have exactly this shape:

arc.view(function()
  -- (optional locals)
  return <widget>
end)

Widgets are plain Lua tables:
ui.col{ pad=N, gap=N, align="start"|"center", child1, child2, ... }   vertical stack
ui.row{ ... }                                                          horizontal stack
ui.label{ text=s, size=1|2|3, color=c }        one line; 6*size px per char, 8*size px tall
ui.text{ text=s, size=1, w=228, color=c }      word-wrapped block
ui.rect{ w=N, h=N, color=c, fill=true|false }
ui.bar{ value=0..1, w=N, h=N, color=c }
ui.space{ w=N, h=N }
ui.canvas{ w=N, h=N, draw=function(x, y) ... end }   free drawing at offset (x,y):
  screen.fill_rect(x,y,w,h,r,g,b)  screen.rect(...)  screen.line(x0,y0,x1,y1,r,g,b)
  screen.triangle(...)  screen.fill_triangle(x0,y0,x1,y1,x2,y2,r,g,b)  screen.pixel(x,y,r,g,b)
Colors c: "white"|"gray"|"dim"|"red"|"green"|"blue"|"amber"|"cyan"|"magenta" or {r,g,b} (0-255).
Conditional child: cond and widget or false.

Readable globals:
  S.text (current story beat), S.a and S.b (choice labels), S.scene (number)
  TILT.x, TILT.y  (≈ -1..1, live accelerometer, updates 10x/second — parallax, sway, a glow that follows the hand)
  arc.pending("turn")  falsy when idle; while the next beat is being written it returns the ask payload table — p.choice is "a" or "b". ALWAYS use this to visibly confirm which option the reader picked (highlight it, dim the other, echo it) with your own waiting microcopy (never the word "Thinking"). The pause must confirm the choice, not just say "busy".
  arc.pending("remix") truthy while a new look is being designed
  arc.dots(n)  animated 1..n counter for motion
  sin cos floor abs min max fract are global functions

The runtime always draws three small amber activity dots in the bottom-right
corner (the last ~8px) while the agent is working — keep that corner clear.
Your own waiting state is in addition to the dots, not a replacement.

HARD RULES:
- Everything fits in 240x135 with >= 6px bottom margin. Story text is ALWAYS ui.text with size=1 and w <= 228 (it can be 140 chars — plan for 4 wrapped lines).
- S.a and S.b must be clearly visible and clearly marked as the two buttons (A/B).
- <= 45 lines and <= 2200 characters total — prefer a few strong decorative elements over many tiny ones. No require/os/io/load/dofile/goto, no while or repeat loops.
- Be bold: new palette, new composition, decoration, motion. Do not imitate the previous look.`

type Turn = { choice?: string; text: string; a: string; b: string }

export class ArcAgent extends DeviceAgent<Env> {
  async onMessage(connection: Connection, data: WSMessage): Promise<void> {
    await super.onMessage(connection, data)
    if (typeof data !== "string") return
    let msg: any
    try { msg = JSON.parse(data) } catch { return }
    this.toMonitors({ dir: "up", t: Date.now(), msg })

    if (msg?.channel !== "app") return
    if (msg.type === "arc:ask") {
      await this.answerAsk(msg.data ?? {})
    } else if (msg.type === "arc:state") {
      await this.ctx.storage.put("mirror", msg.data ?? {})
    }
    // arc:intent needs no handling here — it is the transcript, already
    // echoed to monitors above. A fuller agent would accumulate it as context.
  }

  async onRequest(request: Request): Promise<Response> {
    const url = new URL(request.url)
    const subpath = url.pathname.replace(/^\/devices\/[^/]+/, "")
    if ((subpath === "" || subpath === "/") && request.method === "GET") {
      return new Response(viewerHtml(this.name), {
        headers: { "Content-Type": "text/html; charset=utf-8" },
      })
    }
    if (subpath === "/arc/push" && request.method === "POST") {
      return this.pushGeneration({ resetStory: true, resetLook: true })
    }
    if (subpath === "/arc/remix" && request.method === "POST") {
      // Same code path as the device's shake → arc:ask remix, but surfaced
      // over HTTP so failures come back as text instead of a DO reset.
      try {
        await this.remixLook({ id: 0 })
        const debug = (await this.ctx.storage.get<string[]>("remixDebug")) ?? []
        return Response.json({ ok: true, debug })
      } catch (err: any) {
        console.error("[arc] remix failed:", err)
        return new Response(`remix failed: ${err?.stack ?? err}`, { status: 500 })
      }
    }
    return super.onRequest(request)
  }

  /** Revision loop: ship runtime + logic + the current (possibly
   *  model-written) look as one app. */
  private async pushGeneration(opts: { resetStory: boolean; resetLook?: boolean }): Promise<Response> {
    const devices = Array.from(this.getConnections("device"))
    if (devices.length === 0) {
      return new Response("Device not connected", { status: 503 })
    }
    const gen = "g" + Date.now().toString(36)
    if (opts.resetLook) await this.ctx.storage.delete("viewChunk")
    const viewChunk = (await this.ctx.storage.get<string>("viewChunk")) ?? ""

    // Carry the story across the generation swap by baking the current
    // scene into the pushed code — the new app boots already mid-story, no
    // intro flash, no hydrate round-trip to race. (JSON string literals are
    // valid Lua strings for our payloads: no \uXXXX escapes in this text.)
    let carry = ""
    if (!opts.resetStory) {
      const story = (await this.ctx.storage.get<Turn[]>("story")) ?? [{ ...INTRO }]
      const cur = story[story.length - 1]
      carry =
        `\nS.text = ${JSON.stringify(cur.text)}\nS.a = ${JSON.stringify(cur.a)}` +
        `\nS.b = ${JSON.stringify(cur.b)}\nS.scene = ${story.length}\n`
    }

    const code =
      `arc = { _gen = ${JSON.stringify(gen)} }\n${arcLua}\n${adventureLua}\n${viewChunk}${carry}`
    await this.ctx.storage.put("gen", gen)
    if (opts.resetStory) {
      await this.ctx.storage.put("story", [{ ...INTRO }] satisfies Turn[])
    }
    this.sendToDevice({
      channel: "system",
      type: "app",
      code,
      description: "arc adventure",
    })
    console.log(`[arc] pushed generation ${gen} (${code.length} bytes, remixed look: ${!!viewChunk})`)
    return Response.json({ ok: true, gen, bytes: code.length, remixed: !!viewChunk })
  }

  /** Response loop: answer an arc:ask with inference through the gateway. */
  private async answerAsk(d: any): Promise<void> {
    const gen = await this.ctx.storage.get<string>("gen")
    if (gen && d.gen && d.gen !== gen) {
      console.log(`[arc] dropping ask for stale generation ${d.gen} (current ${gen})`)
      return
    }
    if (d.ask === "remix") return this.remixLook(d)

    const story = (await this.ctx.storage.get<Turn[]>("story")) ?? [{ ...INTRO }]
    const t0 = Date.now()

    const transcript = story
      .map((s) => (s.choice ? `[chose: ${s.choice}]\n${s.text}` : s.text))
      .join("\n")
    const tilt = typeof d.tilt === "number" ? ` (lantern tilt: ${d.tilt})` : ""
    const prompt = `Story so far:\n${transcript}\n\nThe reader chose: "${this.choiceLabel(story, d.choice)}"${tilt}. Continue the story.`

    let scene: z.infer<typeof SceneSchema>
    try {
      const model = createGatewayProvider(createAnthropic, {
        binding: this.env.AI,
        gateway: this.env.CLOUDFLARE_AIG_ID,
      })(ASK_MODEL)
      const { object } = await generateObject({
        model,
        schema: SceneSchema,
        system: STORY_SYSTEM,
        prompt,
        maxOutputTokens: 300,
      })
      scene = object
    } catch (err) {
      console.error("[arc] inference failed:", err)
      return // the device's shipped fallback covers us
    }

    const reply = this.fitReply({
      id: d.id,
      text: scene.text,
      a: scene.a,
      b: scene.b,
      scene: story.length + 1,
    })
    this.sendToDevice({
      channel: "app",
      type: "arc:reply",
      from: "arc-server",
      nonce: crypto.randomUUID().slice(0, 8),
      data: reply,
    })
    console.log(`[arc] answered ask ${d.id} (${d.ask}) in ${Date.now() - t0}ms: ${reply.text}`)

    if (d.ask !== "recap") {
      story.push({ choice: this.choiceLabel(story, d.choice), text: reply.text, a: reply.a, b: reply.b })
      await this.ctx.storage.put("story", story.slice(-12))
    }
  }

  /**
   * Revision loop, model-authored: have a model write a brand-new view
   * function (the whole look of the device — layout, palette, decoration,
   * its own waiting-state microcopy), syntax-check it, splice it into the
   * generation, push, and re-hydrate the story so play continues seamlessly.
   */
  private async remixLook(d: any): Promise<void> {
    const t0 = Date.now()
    const prev = (await this.ctx.storage.get<string>("viewChunk")) ?? "(the shipped default look)"
    const story = (await this.ctx.storage.get<Turn[]>("story")) ?? [{ ...INTRO }]
    const current = story[story.length - 1]

    const model = createGatewayProvider(createAnthropic, {
      binding: this.env.AI,
      gateway: this.env.CLOUDFLARE_AIG_ID,
    })(REMIX_MODEL)

    let chunk: string | null = null
    let feedback = ""
    const debug: string[] = []
    for (let attempt = 0; attempt < 2 && !chunk; attempt++) {
      try {
        const { text } = await generateText({
          model,
          system: VIEW_SYSTEM,
          prompt:
            `Previous look:\n${prev}\n\nCurrent scene for reference: text=${JSON.stringify(current.text)} a=${JSON.stringify(current.a)} b=${JSON.stringify(current.b)} scene=${story.length}\n\nDesign a completely different look.${feedback}`,
          maxOutputTokens: 3500,
        })
        const candidate = text.replace(/^```(lua)?\s*/m, "").replace(/```\s*$/m, "").trim()
        const problem = this.vetViewChunk(candidate)
        if (problem) {
          console.warn(`[arc] remix attempt ${attempt} rejected: ${problem}`)
          debug.push(`attempt ${attempt}: ${problem}\n---\n${candidate.slice(0, 800)}`)
          feedback = `\n\nYour previous attempt was rejected: ${problem}. Try again, obeying the rules exactly.`
        } else {
          chunk = candidate
        }
      } catch (err: any) {
        console.error("[arc] remix inference failed:", err)
        debug.push(`attempt ${attempt} threw: ${err?.message ?? err}`)
        break
      }
    }
    await this.ctx.storage.put("remixDebug", debug)

    if (!chunk) {
      this.sendToDevice({
        channel: "app", type: "arc:reply", from: "arc-server",
        nonce: crypto.randomUUID().slice(0, 8),
        data: { id: d.id, note: "the world resists change" },
      })
      return
    }

    await this.ctx.storage.put("viewChunk", chunk)
    console.log(`[arc] remix accepted in ${Date.now() - t0}ms (${chunk.length} chars)`)
    // Story state is baked into the pushed code (carry-over) — the new
    // generation boots mid-story with the new look, seamlessly.
    await this.pushGeneration({ resetStory: false })
  }

  /** Cheap static vetting for a model-written view chunk. */
  private vetViewChunk(code: string): string | null {
    const body = code.replace(/^\s*(--[^\n]*\n)+/, "")   // leading comments are fine
    if (!/^arc\.view\(function\s*\(\)/.test(body)) return 'chunk must start with arc.view(function()'
    if (code.length > 4000) return "too long"
    if (/\b(require|os|io|load|dofile|loadstring|goto|while|repeat)\b/.test(code)) {
      return "uses a banned construct (require/os/io/load/goto/while/repeat)"
    }
    try {
      luaparse.parse(code, { luaVersion: "5.3" })
    } catch (e: any) {
      return `Lua syntax error: ${e?.message ?? e}`
    }
    return null
  }

  private choiceLabel(story: Turn[], choice: unknown): string {
    const current = story[story.length - 1]
    if (choice === "a") return current?.a ?? "A"
    if (choice === "b") return current?.b ?? "B"
    return String(choice ?? "?")
  }

  /** Trim to a word boundary — mid-word cuts read as glitches on the badge. */
  private trimWords(s: string, max: number): string {
    if (s.length <= max) return s
    const cut = s.slice(0, max)
    const sp = cut.lastIndexOf(" ")
    return (sp > max / 2 ? cut.slice(0, sp) : cut).trim()
  }

  /** Trim fields until the serialized reply fits the device's event buffer. */
  private fitReply<T extends { text: string; a: string; b: string }>(reply: T): T {
    const fit = { ...reply, a: this.trimWords(reply.a, 18), b: this.trimWords(reply.b, 18) }
    let max = 140
    while (JSON.stringify(fit).length > REPLY_BUDGET && max > 20) {
      max -= 10
      fit.text = this.trimWords(fit.text, max)
    }
    return fit
  }

  private sendToDevice(obj: unknown): void {
    const s = JSON.stringify(obj)
    for (const conn of this.getConnections("device")) conn.send(s)
    this.toMonitors({ dir: "down", t: Date.now(), msg: obj })
  }

  private toMonitors(obj: unknown): void {
    const s = JSON.stringify(obj)
    for (const m of this.getConnections("monitor")) m.send(s)
  }
}

export default {
  async fetch(request: Request, env: Env) {
    const res = await routeDeviceRequest(request, env.ArcAgent)
    if (res) return res
    return new Response("Not found", { status: 404 })
  },
} satisfies ExportedHandler<Env>
