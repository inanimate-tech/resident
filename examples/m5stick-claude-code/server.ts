import { Agent, routeAgentRequest } from "agents";
import type { Connection, ConnectionContext } from "agents";

type PermissionEvent = {
  tool: string;
  summary: string;
};

const MAX_SUMMARY_LENGTH = 180;
const MAX_TOOL_LENGTH = 48;

const FIELD_BY_TOOL: Record<string, string> = {
  Bash: "command",
  Edit: "file_path",
  Write: "file_path",
  Read: "file_path",
  Glob: "pattern",
  Grep: "pattern",
  WebFetch: "url",
  WebSearch: "query",
  Task: "description"
};

function derivePermissionEvent(payload: unknown): PermissionEvent | null {
  if (!payload || typeof payload !== "object" || Array.isArray(payload)) return null;
  const record = payload as Record<string, unknown>;

  const tool = record.tool_name;
  if (typeof tool !== "string" || tool.length === 0) return null;

  const rawInput = record.tool_input;
  const input =
    rawInput && typeof rawInput === "object" && !Array.isArray(rawInput)
      ? (rawInput as Record<string, unknown>)
      : {};

  const summary = pickSummary(tool, input).slice(0, MAX_SUMMARY_LENGTH);
  return { tool: tool.slice(0, MAX_TOOL_LENGTH), summary };
}

function pickSummary(tool: string, input: Record<string, unknown>): string {
  const preferredField = FIELD_BY_TOOL[tool];
  if (preferredField) {
    const value = input[preferredField];
    if (typeof value === "string") return value;
  }
  for (const value of Object.values(input)) {
    if (typeof value === "string") return value;
  }
  return "";
}

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

    const { pathname } = new URL(request.url);
    if (pathname.endsWith("/hook/permission-request")) {
      return this.handlePermissionHook(request);
    }
    return this.handleAppPost(request);
  }

  private async handleAppPost(request: Request): Promise<Response> {
    const code = await request.text();
    const frame = JSON.stringify({ type: "app", code });
    const { devices, monitors } = this.broadcastFrame(frame);
    console.log(`Sent app to ${devices} device(s) and ${monitors} monitor(s)`);
    return Response.json({ ok: true });
  }

  private async handlePermissionHook(request: Request): Promise<Response> {
    let payload: unknown = null;
    try {
      payload = await request.json();
    } catch {
      console.warn("permission hook: body is not valid JSON");
      return new Response(null, { status: 204 });
    }

    const event = derivePermissionEvent(payload);
    if (!event) {
      console.warn("permission hook: payload missing or invalid tool_name");
      return new Response(null, { status: 204 });
    }

    const frame = JSON.stringify({
      type: "app_event",
      name: "permission",
      data: event
    });
    const { devices, monitors } = this.broadcastFrame(frame);
    console.log(
      `Permission hook: tool=${event.tool} -> ${devices} device(s), ${monitors} monitor(s)`
    );

    return new Response(null, { status: 204 });
  }

  private broadcastFrame(frame: string): { devices: number; monitors: number } {
    let devices = 0;
    let monitors = 0;
    for (const conn of this.getConnections("device")) {
      conn.send(frame);
      devices++;
    }
    for (const conn of this.getConnections("monitor")) {
      conn.send(frame);
      monitors++;
    }
    return { devices, monitors };
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
