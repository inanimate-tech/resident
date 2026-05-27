import { DeviceAgent, routeDeviceRequest } from "@inanimate/resident/cloudflare"

// Custom DeviceAgent that adds a /register endpoint. The device POSTs here on
// boot (and on every reconnect) and gets back a JSON config blob — for this
// example, just the timezone derived from Cloudflare's edge geolocation.
class ClockAgent extends DeviceAgent {
  async onRequest(request: Request): Promise<Response> {
    const url = new URL(request.url)
    if (url.pathname.endsWith("/register") && request.method === "POST") {
      const timezone =
        (request.cf as { timezone?: string })?.timezone ?? "Etc/UTC"
      return Response.json({ timezone })
    }
    // Fall through to the canonical handler — /send relay, the GET status
    // endpoint, the 404 default.
    return super.onRequest(request)
  }
}

// wrangler.jsonc binds the class named "DeviceAgent"; re-exporting our
// subclass under that name means the template's wrangler.jsonc works as-is.
export { ClockAgent as DeviceAgent }

export default {
  async fetch(request: Request, env: Env) {
    const res = await routeDeviceRequest(request, env.DeviceAgent)
    if (res) return res
    return new Response("Not found", { status: 404 })
  },
} satisfies ExportedHandler<Env>
