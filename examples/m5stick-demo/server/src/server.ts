import { Agent, routeAgentRequest } from "agents";
import type { Connection, ConnectionContext } from "agents";

export class DeviceAgent extends Agent<Env> {
  getConnectionTags(
    _connection: Connection,
    ctx: ConnectionContext
  ): string[] {
    const url = new URL(ctx.request.url);
    if (url.searchParams.get("monitor") === "1") {
      return ["monitor"];
    }
    return ["device"];
  }

  // Device connections are plain WebSocket clients (Courier) — they don't
  // speak the agents SDK protocol, so suppress identity/state frames.
  shouldSendProtocolMessages(
    _connection: Connection,
    ctx: ConnectionContext
  ): boolean {
    const url = new URL(ctx.request.url);
    return url.searchParams.get("monitor") === "1";
  }

  onConnect(_connection: Connection, _ctx: ConnectionContext): void {
    this.broadcastStatus();
  }

  onClose(
    _connection: Connection,
    _code: number,
    _reason: string,
    _wasClean: boolean
  ): void {
    this.broadcastStatus();
  }

  async onRequest(request: Request): Promise<Response> {
    if (request.method !== "POST") {
      return new Response("Method not allowed", { status: 405 });
    }

    const code = await request.text();
    const message = JSON.stringify({ type: "app", code });

    let deviceCount = 0;
    for (const conn of this.getConnections("device")) {
      conn.send(message);
      deviceCount++;
    }
    let monitorCount = 0;
    for (const conn of this.getConnections("monitor")) {
      conn.send(message);
      monitorCount++;
    }
    console.log(`Sent app to ${deviceCount} device(s) and ${monitorCount} monitor(s)`);

    return Response.json({ ok: true });
  }

  private broadcastStatus(): void {
    let deviceConnected = false;
    for (const _conn of this.getConnections("device")) {
      deviceConnected = true;
      break;
    }

    const message = JSON.stringify({ type: "status", deviceConnected });
    for (const conn of this.getConnections("monitor")) {
      conn.send(message);
    }
  }
}

export default {
  async fetch(request: Request, env: Env) {
    return (
      (await routeAgentRequest(request, env)) ||
      new Response("Not found", { status: 404 })
    );
  }
} satisfies ExportedHandler<Env>;
