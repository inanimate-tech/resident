import handler from "@tanstack/react-start/server-entry"
import { routeAgentRequest } from "agents"
import { routeDeviceRequest } from "@inanimate/resident/cloudflare"

export { VoiceAgent } from "./agents/voice-agent"

export default {
  async fetch(request: Request, env: Env, _ctx: ExecutionContext) {
    if (request.headers.get("Upgrade") === "websocket") {
      // /devices/<id>  — physical M5Stick streaming audio (Resident relay).
      const deviceRes = await routeDeviceRequest(request, env.VoiceAgent)
      if (deviceRes) return deviceRes
      // /agents/voice-agent/<id>  — browser monitor via useAgent (Agents SDK).
      const agentRes = await routeAgentRequest(request, env)
      if (agentRes) return agentRes
    }
    return handler.fetch(request)
  },
} satisfies ExportedHandler<Env>
