import { DeviceAgent, routeDeviceRequest } from "@inanimate/resident/cloudflare"

// Cloudflare needs the Durable Object class re-exported at the worker entry
// so it can instantiate it for the binding declared in wrangler.jsonc.
export { DeviceAgent }

export default {
  async fetch(request: Request, env: Env) {
    const res = await routeDeviceRequest(request, env.DeviceAgent)
    if (res) return res
    return new Response("Not found", { status: 404 })
  },
} satisfies ExportedHandler<Env>
