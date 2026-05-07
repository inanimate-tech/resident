import { Agent, getAgentByName } from "agents";
import type { Connection, ConnectionContext, WSMessage } from "agents";

// Self-hostable Worker that mirrors the canonical resident.inanimate.tech
// protocol: a dumb relay that forwards JSON sent via POST /devices/<id>/send
// to the device's WebSocket connection. Adds an opt-in monitor WS for the
// bundled web UI (?monitor=1) — that's an extension on top of the canonical
// protocol, not part of it.

export class DeviceAgent extends Agent<Env> {
  // Tag connections so devices and monitors can be addressed separately.
  getConnectionTags(_connection: Connection, ctx: ConnectionContext): string[] {
    const url = new URL(ctx.request.url);
    if (url.searchParams.get("monitor") === "1") return ["monitor"];
    return ["device"];
  }

  // Devices speak Courier (plain WS); they don't expect agents-SDK identity
  // / state frames. Suppress those for device connections; keep them for the
  // monitor WS so the React useAgent-style hook can use it if anyone wants.
  shouldSendProtocolMessages(_connection: Connection, ctx: ConnectionContext): boolean {
    const url = new URL(ctx.request.url);
    return url.searchParams.get("monitor") === "1";
  }

  onConnect(connection: Connection, ctx: ConnectionContext): void {
    const url = new URL(ctx.request.url);
    if (url.searchParams.get("monitor") === "1") {
      // Newly-connected monitor: send current device status only to it.
      const deviceConnected = Array.from(this.getConnections("device")).length > 0;
      connection.send(JSON.stringify({ type: "status", deviceConnected }));
    } else {
      // A device just connected: notify all monitors.
      this.broadcastStatus();
    }
  }

  onClose(_connection: Connection): void {
    // A device or monitor closed; cheapest-correct behaviour is to broadcast.
    this.broadcastStatus();
  }

  async onMessage(_connection: Connection, _data: WSMessage): Promise<void> {
    // v1: device-originated messages accepted but not relayed.
  }

  async onRequest(request: Request): Promise<Response> {
    const url = new URL(request.url);
    const subpath = url.pathname.replace(/^\/devices\/[^/]+/, "");

    if (subpath === "/send" && request.method === "POST") {
      return this.handleSend(request);
    }
    if ((subpath === "" || subpath === "/") && request.method === "GET") {
      const conns = Array.from(this.getConnections("device")).length;
      return new Response(`deviceId: ${this.name}\nconnections: ${conns}\n`, {
        headers: { "Content-Type": "text/plain; charset=utf-8" },
      });
    }
    return new Response("Not found", { status: 404 });
  }

  private async handleSend(request: Request): Promise<Response> {
    const ct = request.headers.get("Content-Type") || "";
    if (!ct.includes("application/json")) {
      return new Response("Content-Type must be application/json", { status: 415 });
    }
    const raw = await request.text();
    let parsed: unknown;
    try {
      parsed = JSON.parse(raw);
    } catch {
      return new Response("Invalid JSON", { status: 400 });
    }
    if (typeof parsed !== "object" || parsed === null) {
      return new Response("Invalid JSON", { status: 400 });
    }

    const devices = Array.from(this.getConnections("device"));
    if (devices.length === 0) {
      return new Response("Device not connected", { status: 503 });
    }
    for (const conn of devices) conn.send(raw);
    // Echo to monitors so the bundled UI sees what was just pushed.
    for (const conn of this.getConnections("monitor")) conn.send(raw);
    return new Response("OK", { status: 200 });
  }

  private broadcastStatus(): void {
    const deviceConnected = Array.from(this.getConnections("device")).length > 0;
    const message = JSON.stringify({ type: "status", deviceConnected });
    for (const conn of this.getConnections("monitor")) conn.send(message);
  }
}

export default {
  async fetch(request: Request, env: Env) {
    const url = new URL(request.url);

    // Direct route: /devices/<id>/* → DeviceAgent (handles HTTP + WS upgrades).
    if (url.pathname.startsWith("/devices/")) {
      const deviceId = url.pathname.split("/")[2];
      if (!deviceId) return new Response("Device ID required", { status: 400 });
      const agent = await getAgentByName(env.DeviceAgent, deviceId);
      return agent.fetch(request);
    }

    // Everything else falls through to static assets (the React UI),
    // configured via wrangler.jsonc -> assets.
    return new Response("Not found", { status: 404 });
  },
} satisfies ExportedHandler<Env>;
