import handler from "@tanstack/react-start/server-entry"
import { routeDeviceRequest } from "@inanimate/resident/cloudflare"

export { VoiceAgent } from "./agents/voice-agent"

export default {
  async fetch(request: Request, env: Env, ctx: ExecutionContext) {
    // The Resident relay handles /devices/<deviceId> WebSocket upgrades.
    // For non-WS requests on those paths (e.g. GET /devices/<id> from the
    // browser), routeDeviceRequest returns null and we hand off to TanStack.
    const res = await routeDeviceRequest(request, env.VoiceAgent)
    if (res) return res
    return handler.fetch(request, env, ctx)
  },
} satisfies ExportedHandler<Env>
