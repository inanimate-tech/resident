// Self-hostable Worker that mirrors the canonical resident.inanimate.tech
// protocol. The relay logic lives in @inanimate/resident/cloudflare;
// this file just wires it up and re-exports the Durable Object class so
// Cloudflare can instantiate it for the binding declared in wrangler.jsonc.

import { DeviceAgent, routeDeviceRequest } from "@inanimate/resident/cloudflare"

export { DeviceAgent }

export default {
  async fetch(request: Request, env: Env) {
    const res = await routeDeviceRequest(request, env.DeviceAgent)
    if (res) return res
    // Anything else falls through to static assets (the React UI), per
    // wrangler.jsonc -> assets.run_worker_first only routing /devices/*.
    return new Response("Not found", { status: 404 })
  },
} satisfies ExportedHandler<Env>
