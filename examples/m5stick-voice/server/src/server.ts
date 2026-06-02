import handler from "@tanstack/react-start/server-entry"
import { routeDeviceRequest } from "@inanimate/resident/cloudflare"

export { VoiceAgent } from "./agents/voice-agent"

export default {
  async fetch(request: Request, env: Env, _ctx: ExecutionContext) {
    // Forward WebSocket upgrades on /devices/* to the Resident relay (DO).
    // HTTP GETs to /devices/<id> are served by the React route via TanStack.
    if (request.headers.get("Upgrade") === "websocket") {
      const res = await routeDeviceRequest(request, env.VoiceAgent)
      if (res) return res
    }
    return handler.fetch(request)
  },
} satisfies ExportedHandler<Env>
