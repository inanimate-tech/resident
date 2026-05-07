# Resident Sandbox Tooling — Design

**Date:** 2026-05-07
**Status:** Draft

## Goals

Make it cheap to author, validate, and push Lua apps to any Resident-based
device:

1. A device author writes one document — `DEVICE-SKILL.md` — that fully
   describes the device's hardware-specific Lua surface.
2. An agent (Claude Code, etc.) can read that document plus Resident's own
   sandbox docs and produce working Lua apps without needing to know about
   the device beforehand.
3. The author or agent can validate apps locally (no device round-trip) and
   push them to a connected device with a single command.
4. A default hosted relay at `resident.inanimate.tech/devices/<deviceId>`
   removes server setup as a barrier. Self-hosting remains trivial.

## Non-goals (v1)

- Two-way telemetry (compile errors, runtime errors) surfaced back to the
  pushing skill. The relay is fire-and-forget for now.
- Bidirectional protocol negotiation. The relay is dumb and forwards JSON
  verbatim.
- Hawthorn protocol compatibility. These skills speak the Resident
  canonical protocol only. Hawthorn-firmware projects continue to use the
  existing `hawthorn:send-app-to-device` skill until they migrate.
- Multi-device session memory in skills. `push-app` is flag-driven each
  invocation.

## Architecture

```
~/code/<firmware-project>/
├── DEVICE-SKILL.md                # device-specific Lua surface (overlay)
├── device-apps/                   # Lua sources produced by create-app
│   └── <name>.lua
└── ...

tools/agent-plugin/skills/         # in this repo
├── write-device-skill/            # author DEVICE-SKILL.md
├── create-app/                    # NL → Lua source
├── validate-app/                  # Lua source → pass/fail (local lua + stubs)
└── push-app/                      # Lua source → POST /devices/<id>/send

resident-web/ (~/code/resident-web — separate workspace)
└── DeviceAgent DO (Cloudflare Agents SDK)
    ├── WS  wss://.../devices/<id>           ← device firmware connects here
    └── HTTP POST /devices/<id>/send         ← skill posts JSON, relayed verbatim
```

Skills are independent and composable. The agent drives composition:

1. Project missing `DEVICE-SKILL.md` → invoke `write-device-skill`.
2. `create-app --out device-apps/foo.lua "<description>"` → writes Lua.
3. `validate-app device-apps/foo.lua` → exit 0 success, non-zero with
   structured error. Agent re-prompts `create-app` with the error on fail.
4. `push-app --base-url <url> --device-id <id> device-apps/foo.lua` → ships.

### End-to-end principle

Protocol shape (`{type:"app", code:"..."}` and future `shader`,
`app_event`, …) is known to the **firmware library** and the **skill**.
The relay never reads payload contents — it confirms well-formed JSON and
forwards. New message types do not require a relay deploy.

## DEVICE-SKILL.md format

Single Markdown file at the firmware project root. Excludes anything
generic to the Resident sandbox (lifecycle callbacks, `ctx` table, `log.*`,
`time.*`, shader helpers `rgb`/`fract`/`beat`/`noise2d`, math globals) —
those live in Resident's own docs and are embedded by `create-app`.

### Template

```markdown
# <Device name>

<one-paragraph overview: what the device is, what kind of apps suit it>

## Hardware
<physical surface: screens, sensors, audio, indicators, buttons.
Include coordinate frames and units the Lua side will see (g-force, Hz, ms).>

## Lua Modules

### <module-name>.*
**Hardware:** <one-line>
\`\`\`lua
-- Function-by-function reference: signatures, defaults, ranges.
\`\`\`
(Repeat per driver module.)

## Examples
<3–6 short, working Lua apps that exercise the device modules. Tight.>

## Constraints
<screen dimensions, frequency/duration ranges, memory limits — anything
that shapes generated code.>

## Practical Tips
<device-specific idioms (shake detection, menu nav, etc.). Optional.>
```

### Seed example

`examples/m5stick-demo/DEVICE-SKILL.md` ships with the resident repo.
Adapted from `~/code/hawthorn-worker/agents/prompts/stick-app.md` with:

- Hawthorn-specific room/coordination references removed.
- Module surface scoped to what `examples/m5stick-demo/`'s firmware
  actually exposes (`screen`, `imu`, `buzzer`, `button`).
- Sandbox-generic content (lifecycle, `ctx`, `log`, `time`, `kv`) removed
  — covered by `create-app`'s embedded sandbox docs.

## Skills

All under `tools/agent-plugin/skills/`. Each skill is a self-contained
directory with `SKILL.md` and a `tools/` subdirectory for shell scripts.

### `write-device-skill`

**Purpose:** Help a device author produce a `DEVICE-SKILL.md` for their
firmware project.

**Inputs:** none required. Asks the user for the firmware project root
(defaults to cwd) and the device's identity.

**Behavior:**
- Bundles the template above and a reference example
  (`examples/m5stick-demo/DEVICE-SKILL.md`).
- Walks the user through hardware → Lua modules → examples → constraints,
  asking them to describe each in turn.
- Writes `DEVICE-SKILL.md` at the firmware project root.

**v1 scope:** the user describes their device. The skill does not parse
C++ source. Future: scan `Driver` subclasses for `name()` and
`registerModule()` calls to suggest the module surface.

### `create-app`

**Purpose:** Generate Lua source from a natural-language description.

**Inputs:**
- A description of the desired app (positional or via `args`).
- Optional `--out <path>` for the output file. If omitted, prints to stdout.

**Behavior:**
- Embeds a curated subset of `~/code/resident/docs/api.md` covering the
  sandbox surface: lifecycle callbacks (`init`, `on_tick`, `on_event`),
  `ctx` table, built-in modules (`log`, `time`), shader-compatible globals
  (`rgb`, `fract`, `beat`, `noise2d`, math globals).
- Reads `./DEVICE-SKILL.md` from cwd. If missing → tells the user to
  invoke `write-device-skill` first and exits non-zero.
- Generates Lua source; writes to `--out` or stdout.

**v1 scope:** pure generation. No validation, no push. The agent invokes
`validate-app` and `push-app` separately.

### `validate-app`

**Purpose:** Run local checks against Lua source before pushing.

**Inputs:**
- A Lua file path (positional). Reads stdin if no positional given.

**Behavior:**
- Checks for `lua` and `luac` on PATH. If missing, prints a clear hint
  (`brew install lua`) and exits non-zero.
- Reads `./DEVICE-SKILL.md` and scans Lua code blocks to find module
  names referenced (top-level identifiers like `screen`, `imu`, `buzzer`).
- Composes a permissive stub harness:
  - For each device module from `DEVICE-SKILL.md`: a table whose
    `__index` returns a no-op function.
  - For sandbox built-ins (`log.info`, `log.warn`, `log.error`,
    `time.is_valid`, `time.hour`, `time.minute`, `time.second`,
    `time.day_id`, `time.has_timezone`): hardcoded loose stubs.
  - For free functions (`rgb`, `fract`, `beat`, `noise2d`, `floor`,
    `ceil`, `abs`, `sin`, `cos`, `tan`, `sqrt`, `min`, `max`, `fmod`):
    hardcoded.
- Composes harness:
  ```
  <stubs>
  <user app>
  local ctx = { time_ms = 0, trigger_count = 0,
                utc_h = 12, utc_m = 0,
                localtime_h = 12, localtime_m = 0,
                day_id = 1 }
  assert(init or on_tick or on_event,
         "app defines none of init / on_tick / on_event")
  if init    then init(ctx) end
  if on_tick then for i=1,5 do ctx.time_ms = ctx.time_ms + 100; on_tick(ctx, 100) end end
  ```
  `on_event` is not invoked in v1 (would require synthesizing events).
- Runs harness via `lua`.
- Exit 0 on pass; non-zero with structured stderr line:
  `validate-app: FAIL: <message> (line <n>)` on fail.

**Assumption to test during implementation:** that stubs deduced from
`DEVICE-SKILL.md` Lua code blocks are loose-enough to compile any
reasonable app and tight-enough that bugs surface. If too loose or too
coarse, follow-on adds an opt-in `## Validation stubs` Lua block to
`DEVICE-SKILL.md` whose contents replace the auto-deduced stubs.

### `push-app`

**Purpose:** Send a Lua app to a connected device via the relay.

**Inputs:**
- `--base-url <url>` (e.g. `https://resident.inanimate.tech`,
  `http://localhost:8787`). Required.
- `--device-id <id>`. Required.
- A Lua file path (positional). Reads stdin if no positional given.

**Behavior:**
- Reads source from file or stdin.
- Wraps as `{ "type": "app", "code": "<source>" }`.
- `POST <base-url>/devices/<device-id>/send` with
  `Content-Type: application/json`.
- Maps responses:
  - `200` → success; print `Sent.` to stderr.
  - `400` → malformed JSON (bug in skill — shouldn't happen).
  - `415` → wrong Content-Type (bug in skill — shouldn't happen).
  - `503` → device not connected; tell the user.
  - other → print HTTP status and body, exit non-zero.

## Worker (`resident-web`)

Adds `DeviceAgent` to the existing `~/code/resident-web/` worker, parallel
to `StubAgent`.

### Files

```
~/code/resident-web/
├── agents/
│   ├── stub.ts                 # existing
│   └── device.ts               # NEW
├── workers/
│   └── app.ts                  # MODIFIED — direct route + drop Basic auth
└── wrangler.jsonc              # MODIFIED — DeviceAgent binding + migration
```

### `agents/device.ts`

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
    const url = new URL(request.url);
    const subpath = url.pathname.replace(/^\/devices\/[^/]+/, "");

    if (subpath === "/send" && request.method === "POST") {
      return this.handleSend(request);
    }
    return new Response("Not found", { status: 404 });
  }

  private async handleSend(request: Request): Promise<Response> {
    if (!(request.headers.get("Content-Type") || "").includes("application/json")) {
      return new Response("Content-Type must be application/json", { status: 415 });
    }
    let body: string;
    try {
      const parsed = await request.json();
      if (typeof parsed !== "object" || parsed === null) throw new Error("not object");
      body = JSON.stringify(parsed);
    } catch {
      return new Response("Invalid JSON", { status: 400 });
    }

    const conns = Array.from(this.getConnections());
    if (conns.length === 0) {
      return new Response("Device not connected", { status: 503 });
    }
    for (const conn of conns) conn.send(body);
    return new Response("OK", { status: 200 });
  }
}

export default DeviceAgent;
```

The DO is a dumb relay. It does not read or interpret `code`/`expr`/`name`
fields. Only well-formed-JSON validation, then forward verbatim. New
protocol message types work without a DO change.

### `workers/app.ts` (modified)

```typescript
import { createRequestHandler } from "react-router";
import { getAgentByName, routeAgentRequest } from "agents";

export { default as StubAgent } from "../agents/stub";
export { DeviceAgent } from "../agents/device";

declare module "react-router" {
  export interface AppLoadContext {
    cloudflare: { env: Env; ctx: ExecutionContext };
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

    return requestHandler(request, { cloudflare: { env, ctx } });
  },
} satisfies ExportedHandler<Env>;
```

Basic auth is removed entirely: `requireBasicAuth`, `constantTimeEqual`,
`unauthorized` deleted. The `AUTH_PASSWORD` secret can be removed from
production as a separate ops step.

### `wrangler.jsonc` (modified)

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

## Auth model

For v1, **deviceId is the auth.** The skill, the device firmware, and the
hosted relay must share an unguessable deviceId — at least 128 bits of
entropy (e.g. UUID v4 or 32-character hex). Anyone holding the deviceId
can push to or connect as that device. This is "share-link auth": zero
account setup, documented as not-secret-by-default.

The seeded `examples/m5stick-demo` continues to use the guessable
`m5stick-demo` deviceId for demo purposes — fine for self-hosted, not
recommended for the public relay.

Documented in `examples/m5stick-demo/README.md` and the `push-app`
SKILL.md: choose long random IDs for production-style use, treat them
like API keys.

## Endpoints

| URL | Method | Caller | Purpose |
|-----|--------|--------|---------|
| `wss://resident.inanimate.tech/devices/<id>` | WS Upgrade | device firmware | live device connection |
| `https://resident.inanimate.tech/devices/<id>/send` | POST (JSON) | `push-app` skill | relay JSON → device WS |

Both also work against `http://localhost:8787` for local dev.

## Open assumptions

The implementation must validate these; if violated, follow-on work is
called out below.

1. **Stub deduction from `DEVICE-SKILL.md` is sufficient.** validate-app
   parses Lua code blocks to find module names; loose `__index` stubs +
   hardcoded sandbox built-ins should be enough to compile reasonable apps.
   Test by running validate-app against the seed `examples/m5stick-demo`
   apps in `device-apps/`. If too loose / coarse → add opt-in
   `## Validation stubs` Lua block to `DEVICE-SKILL.md`.

2. **DeviceId-as-auth is acceptable for v1.** Test by sharing an example
   deviceId publicly (intentionally) and observing the impact. If
   abuse/noise is meaningful → add device-paired tokens.

3. **Fire-and-forget push is acceptable for v1.** The skill sees "200
   delivered" but not "compile/runtime succeeded on device". Test by
   shipping and noting how often users get confused. If meaningful → add
   a telemetry stream (SSE GET `/devices/<id>/events` or monitor WS).

## Follow-on specs

Out of scope for this spec, listed for visibility:

- Telemetry round-trip (compile errors, runtime errors back to the skill).
- Monitor WebSocket (`?type=monitor` connections that receive a copy of
  every relayed message).
- `write-device-skill` auto-discovery: parse C++ `Driver` subclasses for
  `name()` and `registerModule()` calls.
- Local simulator (run apps without a physical device).
- Device-paired tokens or other stronger auth.

## File layout summary

This spec implies the following changes:

```
resident/                                                # this repo
├── docs/superpowers/specs/
│   └── 2026-05-07-resident-sandbox-tooling-design.md    # THIS DOC
├── examples/m5stick-demo/
│   ├── DEVICE-SKILL.md                                  # NEW (seeded from stick-app.md)
│   └── README.md                                        # MODIFIED — deviceId-as-auth note
└── tools/agent-plugin/skills/
    ├── write-device-skill/
    │   ├── SKILL.md
    │   └── tools/                                       # NEW (template, reference)
    ├── create-app/
    │   ├── SKILL.md
    │   ├── docs/                                        # embedded sandbox subset
    │   └── tools/
    ├── validate-app/
    │   ├── SKILL.md
    │   └── tools/                                       # validate.sh, harness.lua
    └── push-app/
        ├── SKILL.md
        └── tools/                                       # push.sh

resident-web/                                            # ~/code/resident-web
├── agents/
│   └── device.ts                                        # NEW
├── workers/
│   └── app.ts                                           # MODIFIED
└── wrangler.jsonc                                       # MODIFIED
```
