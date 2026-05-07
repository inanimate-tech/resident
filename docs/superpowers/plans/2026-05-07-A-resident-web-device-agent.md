# Plan A — resident-web DeviceAgent Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `DeviceAgent` Durable Object to `~/code/resident-web/` that relays JSON pushes to a connected device's WebSocket. Direct routing for `/devices/<id>/*`. Drop Basic auth.

**Architecture:** Cloudflare Agents SDK `Agent` base class. One DO instance per device id. `onRequest` handles `POST /send`; `onConnect` accepts the device WebSocket. Worker routes `/devices/<id>/*` directly via `getAgentByName(env.DeviceAgent, id).fetch(request)` — no URL rewriting, no `routeAgentRequest` for this path.

**Tech Stack:** TypeScript, Cloudflare Workers, Durable Objects, Cloudflare Agents SDK (`agents@^0.11.5`), Wrangler.

**Spec reference:** `~/code/resident/docs/superpowers/specs/2026-05-07-resident-sandbox-tooling-design.md` § "Worker (`resident-web`)".

**Working directory for this plan:** `~/code/resident-web/` (separate workspace from the resident repo).

---

## File Structure

| File | Action | Purpose |
|------|--------|---------|
| `agents/device.ts` | create | `DeviceAgent` class (relay logic) |
| `workers/app.ts` | modify | Direct `/devices/<id>/*` route; drop Basic auth |
| `wrangler.jsonc` | modify | Add `DeviceAgent` DO binding + migration tag `v2` |

Test rig: integration tests via `wrangler dev` + `curl` + `websocat` (no vitest). All test commands assume `wrangler dev` is running on `http://localhost:8787` in a side terminal.

---

### Task 1: Create DeviceAgent skeleton with no-op handlers

**Files:**
- Create: `~/code/resident-web/agents/device.ts`

- [ ] **Step 1: Write the file**

```typescript
import { Agent, type Connection, type ConnectionContext, type WSMessage } from "agents";

export class DeviceAgent extends Agent {
  onConnect(_connection: Connection, _ctx: ConnectionContext) {
    // Device-side WS attached. No registration; deviceId is in the URL path.
  }

  async onMessage(_connection: Connection, _data: WSMessage) {
    // v1: device-originated messages accepted but not relayed.
    // Future: forward to monitor connections / persist for fetch.
  }

  async onRequest(request: Request): Promise<Response> {
    return new Response("Not found", { status: 404 });
  }
}

export default DeviceAgent;
```

- [ ] **Step 2: Wire DeviceAgent into wrangler.jsonc**

Edit `~/code/resident-web/wrangler.jsonc`. Replace the existing `durable_objects` and `migrations` blocks:

```jsonc
"durable_objects": {
  "bindings": [
    { "name": "StubAgent",   "class_name": "StubAgent" },
    { "name": "DeviceAgent", "class_name": "DeviceAgent" }
  ]
},
"migrations": [
  { "tag": "v1", "new_sqlite_classes": ["StubAgent"] },
  { "tag": "v2", "new_sqlite_classes": ["DeviceAgent"] }
]
```

- [ ] **Step 3: Export DeviceAgent from workers/app.ts**

In `~/code/resident-web/workers/app.ts`, add the export line under the existing `StubAgent` export:

```typescript
export { default as StubAgent } from "../agents/stub";
export { DeviceAgent } from "../agents/device";
```

- [ ] **Step 4: Regenerate wrangler types**

Run: `cd ~/code/resident-web && npm run cf-typegen`
Expected: completes without error. `worker-configuration.d.ts` now includes `DeviceAgent` in `Env`.

- [ ] **Step 5: Typecheck**

Run: `cd ~/code/resident-web && npm run typecheck`
Expected: zero errors.

- [ ] **Step 6: Commit**

```bash
cd ~/code/resident-web
git add agents/device.ts workers/app.ts wrangler.jsonc worker-configuration.d.ts
git commit -m "feat(agents): add DeviceAgent skeleton with no-op handlers"
```

---

### Task 2: Direct-route /devices/<id>/* and drop Basic auth

**Files:**
- Modify: `~/code/resident-web/workers/app.ts`

- [ ] **Step 1: Replace the file contents**

```typescript
import { createRequestHandler } from "react-router";
import { getAgentByName, routeAgentRequest } from "agents";

export { default as StubAgent } from "../agents/stub";
export { DeviceAgent } from "../agents/device";

declare module "react-router" {
  export interface AppLoadContext {
    cloudflare: {
      env: Env;
      ctx: ExecutionContext;
    };
  }
}

const requestHandler = createRequestHandler(
  () => import("virtual:react-router/server-build"),
  import.meta.env.MODE,
);

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);

    // Direct route: /devices/<id>/* → DeviceAgent
    if (url.pathname.startsWith("/devices/")) {
      const deviceId = url.pathname.split("/")[2];
      if (!deviceId) return new Response("Device ID required", { status: 400 });
      const agent = await getAgentByName(env.DeviceAgent, deviceId);
      return agent.fetch(request);
    }

    // Other agents (StubAgent etc.) keep working via the generic router.
    const agentResponse = await routeAgentRequest(request, env);
    if (agentResponse) return agentResponse;

    return requestHandler(request, {
      cloudflare: { env, ctx },
    });
  },
} satisfies ExportedHandler<Env>;
```

This deletes `requireBasicAuth`, `constantTimeEqual`, `unauthorized`, and the auth call in `fetch`. They're gone entirely, not commented out.

- [ ] **Step 2: Typecheck**

Run: `cd ~/code/resident-web && npm run typecheck`
Expected: zero errors.

- [ ] **Step 3: Smoke-test the route in dev**

In one terminal: `cd ~/code/resident-web && npx wrangler dev`
Wait for the local server to come up on `http://localhost:8787`.

In another terminal:

```bash
curl -i http://localhost:8787/devices/test-1234/
```

Expected: `404 Not Found` with body `Not found`. (DeviceAgent's `onRequest` returns 404 for any unrecognized path; the route reaches it.)

```bash
curl -i http://localhost:8787/devices//
```

Expected: `400 Device ID required`.

```bash
curl -i http://localhost:8787/some-other-path
```

Expected: a react-router response (200 with HTML, or whatever the app renders) — confirms the fall-through still works.

Stop `wrangler dev`.

- [ ] **Step 4: Commit**

```bash
cd ~/code/resident-web
git add workers/app.ts
git commit -m "feat(workers): direct-route /devices/<id>/* to DeviceAgent; drop Basic auth"
```

---

### Task 3: Implement POST /devices/<id>/send relay logic

**Files:**
- Modify: `~/code/resident-web/agents/device.ts`

- [ ] **Step 1: Replace the file contents**

```typescript
import { Agent, type Connection, type ConnectionContext, type WSMessage } from "agents";

export class DeviceAgent extends Agent {
  onConnect(_connection: Connection, _ctx: ConnectionContext) {
    // Device-side WS attached. No registration; deviceId is in the URL path.
  }

  async onMessage(_connection: Connection, _data: WSMessage) {
    // v1: device-originated messages accepted but not relayed.
  }

  async onRequest(request: Request): Promise<Response> {
    const url = new URL(request.url);
    const subpath = url.pathname.replace(/^\/devices\/[^/]+/, "");

    if (subpath === "/send" && request.method === "POST") {
      return this.handleSend(request);
    }
    return new Response("Not found", { status: 404 });
  }

  private async handleSend(request: Request): Promise<Response> {
    const contentType = request.headers.get("Content-Type") || "";
    if (!contentType.includes("application/json")) {
      return new Response("Content-Type must be application/json", { status: 415 });
    }

    let body: string;
    try {
      const parsed = await request.json();
      if (typeof parsed !== "object" || parsed === null) {
        throw new Error("not object");
      }
      body = JSON.stringify(parsed);
    } catch {
      return new Response("Invalid JSON", { status: 400 });
    }

    const conns = Array.from(this.getConnections());
    if (conns.length === 0) {
      return new Response("Device not connected", { status: 503 });
    }
    for (const conn of conns) {
      conn.send(body);
    }
    return new Response("OK", { status: 200 });
  }
}

export default DeviceAgent;
```

- [ ] **Step 2: Typecheck**

Run: `cd ~/code/resident-web && npm run typecheck`
Expected: zero errors.

- [ ] **Step 3: Test 415 on wrong content-type (no devices needed)**

Start `wrangler dev` in one terminal. In another:

```bash
curl -i -X POST http://localhost:8787/devices/test-1234/send -d 'hello'
```

Expected: `415 Content-Type must be application/json`.

- [ ] **Step 4: Test 400 on malformed JSON**

```bash
curl -i -X POST http://localhost:8787/devices/test-1234/send \
  -H 'Content-Type: application/json' \
  -d 'not json'
```

Expected: `400 Invalid JSON`.

```bash
curl -i -X POST http://localhost:8787/devices/test-1234/send \
  -H 'Content-Type: application/json' \
  -d '"a string"'
```

Expected: `400 Invalid JSON` (non-object root rejected).

- [ ] **Step 5: Test 503 when device not connected**

```bash
curl -i -X POST http://localhost:8787/devices/test-1234/send \
  -H 'Content-Type: application/json' \
  -d '{"type":"app","code":"function on_tick() end"}'
```

Expected: `503 Device not connected`.

- [ ] **Step 6: Test 200 + relay with a connected WebSocket**

Install `websocat` if missing: `brew install websocat`.

In one terminal (keep `wrangler dev` running), start a fake device:

```bash
websocat ws://localhost:8787/devices/test-1234
```

This should connect and stay open. In a third terminal:

```bash
curl -i -X POST http://localhost:8787/devices/test-1234/send \
  -H 'Content-Type: application/json' \
  -d '{"type":"app","code":"function on_tick() end"}'
```

Expected curl response: `200 OK`.
Expected websocat output: the JSON message printed verbatim:
```
{"type":"app","code":"function on_tick() end"}
```

Stop `websocat` and `wrangler dev`.

- [ ] **Step 7: Commit**

```bash
cd ~/code/resident-web
git add agents/device.ts
git commit -m "feat(agents): DeviceAgent relays JSON via POST /devices/<id>/send"
```

---

### Task 4: Pre-deploy verification + deploy notes

**Files:** none modified — verification only.

- [ ] **Step 1: Confirm the existing AUTH_PASSWORD secret is no longer required**

The Worker no longer reads `AUTH_PASSWORD`. The secret can be deleted later (separate ops step), but its presence does not break anything. Document this in the commit message of the next step.

- [ ] **Step 2: Final typecheck and build**

Run:
```bash
cd ~/code/resident-web
npm run typecheck
npm run build
```
Expected: both succeed with zero errors.

- [ ] **Step 3: Tag the commit with deploy instructions in the body**

Create an empty commit that records deploy steps (so the next operator has them inline):

```bash
cd ~/code/resident-web
git commit --allow-empty -m "chore: ready to deploy DeviceAgent

Deploy steps:
- npm run deploy
- Optional: wrangler secret delete AUTH_PASSWORD
- Verify: curl https://resident.inanimate.tech/devices/test/send returns 503
- Verify: websocat wss://resident.inanimate.tech/devices/test stays open"
```

- [ ] **Step 4: Optional — actual deploy (only when user requests)**

Do **not** deploy without explicit user instruction. The plan stops here; deploy is a follow-on action.

---

## Self-review

- **Spec coverage:** All four bullet points in spec § "Worker" are addressed: `agents/device.ts` (Task 1, 3), `workers/app.ts` direct route + drop auth (Task 2), `wrangler.jsonc` binding + migration (Task 1). Endpoint table in spec matches Task 3 tests.
- **Placeholders:** none — every step shows code or exact commands.
- **Type consistency:** `DeviceAgent` class name, `handleSend` private method name, JSON message shape `{type, code}` consistent across tasks.
