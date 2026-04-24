# Claude Code Permission Hook Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a hook endpoint to the `DeviceAgent` Durable Object that Claude Code's `PermissionRequest` HTTP hook can POST to. The endpoint broadcasts a `permission` Lua `app_event` to the connected device and returns 204.

**Architecture:** Pure summary-derivation helper (`hook.ts`) is unit-tested with vitest. The DO's `onRequest` dispatches by URL path — existing `/agents/device-agent/:id` (text/plain → Lua app) stays as-is; new `/agents/device-agent/:id/hook/permission-request` (application/json → app_event broadcast) is added. No device firmware changes.

**Tech Stack:** TypeScript, Cloudflare Workers, `agents` SDK (Durable Objects), vitest.

**Spec:** `examples/m5stick-claude-code/docs/superpowers/specs/2026-04-24-claude-code-permission-hook-design.md`

**⚠ User instruction — do NOT commit during task execution.** The user has asked to hold commits on implementation changes until they review the combined diff. Skip any `git add` / `git commit` steps per task. At the end of the plan, run `git status` and show the user the diff so they can decide how to stage and commit.

**Working directory for all commands:** `examples/m5stick-claude-code/server` unless otherwise noted.

---

## File Structure

Files created or modified by this plan:

- Create `examples/m5stick-claude-code/server/src/hook.ts` — pure function `derivePermissionEvent(payload: unknown): { tool: string; summary: string } | null`. No Worker imports; easy to unit-test.
- Create `examples/m5stick-claude-code/server/src/hook.test.ts` — vitest unit tests for the helper above.
- Create `examples/m5stick-claude-code/server/vitest.config.ts` — minimal vitest config (Node environment).
- Modify `examples/m5stick-claude-code/server/package.json` — add `vitest` devDependency and `"test": "vitest run"` script.
- Modify `examples/m5stick-claude-code/server/src/server.ts` — in `onRequest`, dispatch by URL path; add `handlePermissionHook` that calls `derivePermissionEvent`, broadcasts an `app_event` frame to `device` and `monitor` connections, returns `204`.
- Modify `examples/m5stick-claude-code/README.md` — add a "Claude Code permission notifier (optional)" section with the hook config snippet and a curl test command.

---

## Task 1: Scaffold vitest

**Files:**
- Modify: `examples/m5stick-claude-code/server/package.json`
- Create: `examples/m5stick-claude-code/server/vitest.config.ts`

- [ ] **Step 1: Add vitest to devDependencies and a `test` script**

Open `examples/m5stick-claude-code/server/package.json` and:

1. Add `"test": "vitest run"` to the `scripts` object (keep other scripts).
2. Add `"vitest": "^2.1.0"` to `devDependencies` (alphabetical order, i.e. after `typescript` or wherever it lands alphabetically — the existing file isn't strictly alphabetical, just add it in a sensible place).

Final `scripts` section should contain at least:

```json
"scripts": {
  "dev": "vite dev",
  "start": "vite dev",
  "deploy": "vite build && wrangler deploy",
  "types": "wrangler types env.d.ts --include-runtime false",
  "test": "vitest run"
}
```

- [ ] **Step 2: Create vitest config**

Create `examples/m5stick-claude-code/server/vitest.config.ts`:

```ts
import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    environment: "node",
    include: ["src/**/*.test.ts"]
  }
});
```

- [ ] **Step 3: Install**

Run from `examples/m5stick-claude-code/server`:

```bash
npm install
```

Expected: exits 0, adds vitest to `node_modules`.

- [ ] **Step 4: Sanity check**

Run:

```bash
npm test
```

Expected: vitest reports "No test files found". Exit code 1 is fine here — we haven't added tests yet. If you get any other error (e.g. "vitest: not found"), fix the install before continuing.

---

## Task 2: Write failing tests for `derivePermissionEvent`

**Files:**
- Create: `examples/m5stick-claude-code/server/src/hook.ts` (stub only)
- Create: `examples/m5stick-claude-code/server/src/hook.test.ts`

- [ ] **Step 1: Create the stub module**

Create `examples/m5stick-claude-code/server/src/hook.ts`:

```ts
export type PermissionEvent = {
  tool: string;
  summary: string;
};

export function derivePermissionEvent(
  _payload: unknown
): PermissionEvent | null {
  return null;
}
```

- [ ] **Step 2: Write the tests**

Create `examples/m5stick-claude-code/server/src/hook.test.ts`:

```ts
import { describe, expect, test } from "vitest";
import { derivePermissionEvent } from "./hook";

describe("derivePermissionEvent", () => {
  test("Bash payload picks command as summary", () => {
    const result = derivePermissionEvent({
      tool_name: "Bash",
      tool_input: { command: "rm -rf node_modules", description: "clean" }
    });
    expect(result).toEqual({ tool: "Bash", summary: "rm -rf node_modules" });
  });

  test("Edit payload picks file_path as summary", () => {
    const result = derivePermissionEvent({
      tool_name: "Edit",
      tool_input: {
        file_path: "/Users/matt/code/x.ts",
        old_string: "a",
        new_string: "b"
      }
    });
    expect(result).toEqual({
      tool: "Edit",
      summary: "/Users/matt/code/x.ts"
    });
  });

  test("Write payload picks file_path as summary", () => {
    const result = derivePermissionEvent({
      tool_name: "Write",
      tool_input: { file_path: "/tmp/out.txt", content: "hi" }
    });
    expect(result).toEqual({ tool: "Write", summary: "/tmp/out.txt" });
  });

  test("Read payload picks file_path as summary", () => {
    const result = derivePermissionEvent({
      tool_name: "Read",
      tool_input: { file_path: "/tmp/in.txt" }
    });
    expect(result).toEqual({ tool: "Read", summary: "/tmp/in.txt" });
  });

  test("Glob payload picks pattern as summary", () => {
    const result = derivePermissionEvent({
      tool_name: "Glob",
      tool_input: { pattern: "**/*.ts" }
    });
    expect(result).toEqual({ tool: "Glob", summary: "**/*.ts" });
  });

  test("Grep payload picks pattern as summary", () => {
    const result = derivePermissionEvent({
      tool_name: "Grep",
      tool_input: { pattern: "TODO", path: "src" }
    });
    expect(result).toEqual({ tool: "Grep", summary: "TODO" });
  });

  test("WebFetch payload picks url as summary", () => {
    const result = derivePermissionEvent({
      tool_name: "WebFetch",
      tool_input: { url: "https://example.com", prompt: "summarise" }
    });
    expect(result).toEqual({
      tool: "WebFetch",
      summary: "https://example.com"
    });
  });

  test("WebSearch payload picks query as summary", () => {
    const result = derivePermissionEvent({
      tool_name: "WebSearch",
      tool_input: { query: "cloudflare workers" }
    });
    expect(result).toEqual({
      tool: "WebSearch",
      summary: "cloudflare workers"
    });
  });

  test("Task payload picks description as summary", () => {
    const result = derivePermissionEvent({
      tool_name: "Task",
      tool_input: { description: "audit deps", prompt: "..." }
    });
    expect(result).toEqual({ tool: "Task", summary: "audit deps" });
  });

  test("unknown tool falls back to first string field in tool_input", () => {
    const result = derivePermissionEvent({
      tool_name: "SomeCustomTool",
      tool_input: { foo: 1, bar: "hello", baz: "world" }
    });
    expect(result).toEqual({ tool: "SomeCustomTool", summary: "hello" });
  });

  test("unknown tool with no string fields yields empty summary", () => {
    const result = derivePermissionEvent({
      tool_name: "NumericTool",
      tool_input: { count: 3, flag: true }
    });
    expect(result).toEqual({ tool: "NumericTool", summary: "" });
  });

  test("known tool whose preferred field is missing falls back to any string", () => {
    const result = derivePermissionEvent({
      tool_name: "Bash",
      tool_input: { description: "no command here" }
    });
    expect(result).toEqual({ tool: "Bash", summary: "no command here" });
  });

  test("summary longer than 180 chars is truncated", () => {
    const long = "x".repeat(500);
    const result = derivePermissionEvent({
      tool_name: "Bash",
      tool_input: { command: long }
    });
    expect(result?.tool).toBe("Bash");
    expect(result?.summary.length).toBe(180);
    expect(result?.summary).toBe("x".repeat(180));
  });

  test("missing tool_name returns null", () => {
    const result = derivePermissionEvent({
      tool_input: { command: "oops" }
    });
    expect(result).toBeNull();
  });

  test("non-string tool_name returns null", () => {
    const result = derivePermissionEvent({
      tool_name: 42,
      tool_input: { command: "oops" }
    });
    expect(result).toBeNull();
  });

  test("missing tool_input is treated as empty", () => {
    const result = derivePermissionEvent({ tool_name: "Bash" });
    expect(result).toEqual({ tool: "Bash", summary: "" });
  });

  test("non-object tool_input is treated as empty", () => {
    const result = derivePermissionEvent({
      tool_name: "Bash",
      tool_input: "not an object"
    });
    expect(result).toEqual({ tool: "Bash", summary: "" });
  });

  test("null payload returns null", () => {
    expect(derivePermissionEvent(null)).toBeNull();
  });

  test("non-object payload returns null", () => {
    expect(derivePermissionEvent("not json")).toBeNull();
    expect(derivePermissionEvent(42)).toBeNull();
  });
});
```

- [ ] **Step 3: Run tests and confirm they fail**

Run from `examples/m5stick-claude-code/server`:

```bash
npm test
```

Expected: all 19 tests fail (the stub returns `null` for everything; tests expect concrete values). Exit code non-zero.

---

## Task 3: Implement `derivePermissionEvent` to pass tests

**Files:**
- Modify: `examples/m5stick-claude-code/server/src/hook.ts`

- [ ] **Step 1: Replace the stub with the real implementation**

Rewrite `examples/m5stick-claude-code/server/src/hook.ts` in full:

```ts
export type PermissionEvent = {
  tool: string;
  summary: string;
};

const MAX_SUMMARY_LENGTH = 180;

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

export function derivePermissionEvent(
  payload: unknown
): PermissionEvent | null {
  if (!payload || typeof payload !== "object") return null;
  const record = payload as Record<string, unknown>;

  const tool = record.tool_name;
  if (typeof tool !== "string" || tool.length === 0) return null;

  const rawInput = record.tool_input;
  const input =
    rawInput && typeof rawInput === "object" && !Array.isArray(rawInput)
      ? (rawInput as Record<string, unknown>)
      : {};

  const summary = pickSummary(tool, input).slice(0, MAX_SUMMARY_LENGTH);
  return { tool, summary };
}

function pickSummary(
  tool: string,
  input: Record<string, unknown>
): string {
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
```

- [ ] **Step 2: Run tests and confirm they pass**

Run from `examples/m5stick-claude-code/server`:

```bash
npm test
```

Expected: all 19 tests pass. Exit code 0.

If any fail, fix the implementation (not the tests) until they pass. The tests are the spec.

---

## Task 4: Wire the helper into `DeviceAgent.onRequest`

**Files:**
- Modify: `examples/m5stick-claude-code/server/src/server.ts`

Note: the existing `server.ts` is shown below for orientation. The current `onRequest` handles a single behaviour (text/plain body → Lua app broadcast). We're splitting it by URL path.

- [ ] **Step 1: Add an import for the helper at the top of the file**

At the top of `examples/m5stick-claude-code/server/src/server.ts`, after the existing imports:

```ts
import { derivePermissionEvent } from "./hook";
```

- [ ] **Step 2: Replace `onRequest` with a path-dispatching version**

Current `onRequest` (lines 39-60) looks like:

```ts
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
```

Replace the entire method with:

```ts
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
  console.log(
    `Sent app to ${deviceCount} device(s) and ${monitorCount} monitor(s)`
  );

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
    console.warn("permission hook: payload missing tool_name");
    return new Response(null, { status: 204 });
  }

  const frame = JSON.stringify({
    type: "app_event",
    name: "permission",
    data: event
  });

  let deviceCount = 0;
  for (const conn of this.getConnections("device")) {
    conn.send(frame);
    deviceCount++;
  }
  let monitorCount = 0;
  for (const conn of this.getConnections("monitor")) {
    conn.send(frame);
    monitorCount++;
  }
  console.log(
    `Permission hook: tool=${event.tool} -> ${deviceCount} device(s), ${monitorCount} monitor(s)`
  );

  return new Response(null, { status: 204 });
}
```

Keep all other methods (`getConnectionTags`, `shouldSendProtocolMessages`, `onConnect`, `onClose`, `broadcastStatus`) and the default export unchanged.

- [ ] **Step 3: Type-check the worker**

Run from `examples/m5stick-claude-code/server`:

```bash
npx tsc --noEmit
```

Expected: exit code 0, no errors.

If there are errors, fix them. Common slip-ups:
- Forgetting the `derivePermissionEvent` import.
- Accidentally leaving the old `onRequest` body above or below the new one.

- [ ] **Step 4: Manual smoke test against local dev**

Start the worker dev server in one terminal:

```bash
cd examples/m5stick-claude-code/server && npm run dev
```

Wait for vite to print the local URL (typically `http://localhost:5173`).

In a second terminal, open a monitor WebSocket against the DO using `websocat` or a browser. Simplest option: open `http://localhost:5173` in a browser and enter `m5stick-demo` as the device ID so the web UI establishes a monitor connection.

Then POST a synthetic hook payload:

```bash
curl -i -X POST \
  -H "Content-Type: application/json" \
  -d '{"tool_name":"Bash","tool_input":{"command":"rm -rf node_modules"}}' \
  http://localhost:5173/agents/device-agent/m5stick-demo/hook/permission-request
```

Expected:
- HTTP response is `HTTP/1.1 204 No Content` with empty body.
- The worker console logs `Permission hook: tool=Bash -> 0 device(s), 1 monitor(s)` (or whatever counts match your connections).
- In the browser devtools network tab, the monitor WebSocket receives a frame whose body is the JSON `{"type":"app_event","name":"permission","data":{"tool":"Bash","summary":"rm -rf node_modules"}}`.

If the web UI doesn't expose that frame, you can also run a standalone WebSocket client:

```bash
npx wscat -c "ws://localhost:5173/agents/device-agent/m5stick-demo?monitor=1"
```

and watch for the frame after issuing the curl.

Also verify that the existing Lua-app POST still works:

```bash
echo 'function init(ctx) screen.clear(0,0,0) screen.flip() end' | \
  curl -X POST -H "Content-Type: text/plain" --data-binary @- \
  http://localhost:5173/agents/device-agent/m5stick-demo
```

Expected: `{"ok":true}` — the existing endpoint is untouched.

If anything is off, stop and fix before moving on.

---

## Task 5: Add the README section

**Files:**
- Modify: `examples/m5stick-claude-code/README.md`

- [ ] **Step 1: Append the new section after `send-app.sh`**

Open `examples/m5stick-claude-code/README.md`. At the end of the file (after the `send-app.sh` section), append:

````markdown

## Claude Code permission notifier (optional)

Get a nudge on your M5Stick every time [Claude Code](https://claude.com/claude-code) asks for permission to run a tool. Add this to your project's `.claude/settings.json` (or `~/.claude/settings.json` for every session):

```json
{
  "hooks": {
    "PermissionRequest": [
      {
        "hooks": [
          {
            "type": "http",
            "url": "https://m5stick-demo.genmon.workers.dev/agents/device-agent/m5stick-demo/hook/permission-request"
          }
        ]
      }
    ]
  }
}
```

Claude Code POSTs the permission payload to the Worker, which broadcasts a `permission` app event to the connected device. The hook always returns `204 No Content`, so Claude Code still prompts you in the terminal — you approve or deny there as normal.

The currently-loaded Lua app receives the event via `on_event`:

```lua
function on_event(ctx, e)
  if e.name == "permission" then
    -- e.data.tool     string, e.g. "Bash"
    -- e.data.summary  string, e.g. "rm -rf node_modules"
  end
end
```

### Test it without Claude Code

With the device connected, send a synthetic payload:

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"tool_name":"Bash","tool_input":{"command":"rm -rf node_modules"}}' \
  https://m5stick-demo.genmon.workers.dev/agents/device-agent/m5stick-demo/hook/permission-request
```

The device's currently-loaded Lua app will receive an event with `e.name == "permission"` and `e.data == { tool = "Bash", summary = "rm -rf node_modules" }`.
````

Save the file.

---

## Wrap-Up: Show the user the diff

- [ ] **Step 1: Run `git status` and show the user what changed**

Run from the repo root:

```bash
git status
```

- [ ] **Step 2: Summarise the changes for the user**

Produce a short summary listing:
- Files created (`server/src/hook.ts`, `server/src/hook.test.ts`, `server/vitest.config.ts`).
- Files modified (`server/src/server.ts`, `server/package.json`, `README.md`).
- Test results: "19/19 tests pass".
- Local smoke test result: "204 returned, monitor WS received app_event frame".

Ask the user how they want to commit — e.g. as a single commit on the `claude-code-permission-hook` branch, or they'll handle it themselves. **Do not run `git add` or `git commit`** until the user answers.

---

## Coverage Check (Spec → Tasks)

- ✅ "New endpoint `POST /agents/device-agent/:id/hook/permission-request`" → Task 4 Step 2 (path dispatch).
- ✅ "Parses hook payload, extracts tool_name, derives summary" → Task 3 (`derivePermissionEvent`).
- ✅ "Per-tool field mapping (Bash → command, Edit → file_path, …)" → Task 3 (`FIELD_BY_TOOL`), covered by tests in Task 2.
- ✅ "Truncate to 180 chars" → Task 3 `MAX_SUMMARY_LENGTH`, test in Task 2.
- ✅ "Broadcast `{type:"app_event", name:"permission", data:{tool, summary}}` to device and monitor" → Task 4 Step 2 (`handlePermissionHook`).
- ✅ "Always return 204" → Task 4 Step 2.
- ✅ "Malformed JSON / missing tool_name → log and return 204, no broadcast" → Task 4 Step 2 (try/catch + null check), tests in Task 2.
- ✅ "Existing Lua-app POST behaviour unchanged" → Task 4 Step 2 preserves it as `handleAppPost`; smoke test in Step 4.
- ✅ "README section with install snippet + curl test" → Task 5.
- ✅ "No device firmware changes" → no device tasks.
- ✅ "Tests for all documented cases" → Task 2.
