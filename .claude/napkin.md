# Napkin

## Corrections
| Date | Source | What Went Wrong | What To Do Instead |
|------|--------|----------------|-------------------|
| 2026-04-24 | user | Claimed Claude Code has no `PermissionRequest` hook; suggested Notification/PreToolUse instead | `PermissionRequest` IS a real hook event per https://code.claude.com/docs/en/hooks. Payload: `tool_name`, `tool_input`, `permission_suggestions`, `session_id`, `cwd`, `permission_mode`. Fires when a permission dialog is about to show. A no-op (exit 0, no stdout) leaves the dialog intact. Always check the docs before claiming a hook doesn't exist. |
| 2026-04-24 | user | Planned a local hook script (bash/node) to POST to a server when Claude Code hooks can be declared as HTTP directly | Claude Code hooks support `"type": "http"` with a `url` in settings.json. Claude Code itself POSTs the event JSON payload to the URL with `Content-Type: application/json`. Non-2xx / timeout / network errors are **non-blocking**. No-op = 2xx with empty body (204 works). Decisions live in the JSON response body. Check for HTTP hook support before reaching for a local script. |
| 2026-04-24 | self | Dismissed an implementer's note that a helper needed to be non-`private` on a subclass of `Agent` (from the `agents` SDK) — was wrong; forced it to `private`, got `TS2415: Property 'broadcast' is private in type … but not in type 'Agent<…>'`. `Agent` exposes its own `broadcast` method, so naming a subclass helper `broadcast` silently overrides it and can't be narrowed to `private`. | When a subagent flags a constraint I think is wrong, **verify first** (run the compiler or check the SDK type) before overriding them. For this codebase: the `agents` SDK `Agent` class has a `broadcast` method — pick a different name (`broadcastFrame` etc.) to avoid shadowing. |

## User Preferences
- When user cites a doc URL, fetch it before pushing back.

## Patterns That Work
- (accumulate)

## Patterns That Don't Work
- (accumulate)

## Domain Notes
- ESP-IDF `idf_component_register()` derives the component name from the leaf directory containing the CMakeLists.txt — NOT from the manifest key in idf_component.yml. So when an example uses `path: ../../..` to depend on resident, the leaf dir of the resolved path must be `resident`. Inside a git worktree at `.worktrees/espidf-example/`, the leaf is `espidf-example` and `REQUIRES resident` fails to resolve. Fix: name the worktree dir `resident` (e.g. `git worktree add .worktrees/resident -b <branch>`). The branch and dir names can differ.
- ESP-IDF caches its sdkconfig at the project root (next to CMakeLists.txt), NOT just in `build/`. Doing `rm -rf build` does NOT refresh the config — sdkconfig.defaults only gets re-applied if the project-root `sdkconfig` is also deleted. To force a clean config: `rm -f sdkconfig && rm -rf build && idf.py build`.
- arduino-esp32's `libraries/NetworkClientSecure/src/ssl_client.cpp` wraps its entire body in `#if defined(MBEDTLS_KEY_EXCHANGE_SOME_PSK_ENABLED)` — without at least one of `CONFIG_MBEDTLS_PSK_MODES` + a `CONFIG_MBEDTLS_KEY_EXCHANGE_*_PSK` enabled in sdkconfig, the file compiles to an empty .obj and `NetworkClientSecure.cpp` fails to link with `undefined reference to start_ssl_client/ssl_init/...`. The `#warning` it emits is silent enough to miss. Add `CONFIG_MBEDTLS_PSK_MODES=y` and `CONFIG_MBEDTLS_KEY_EXCHANGE_PSK=y` to sdkconfig.defaults.
- Esp32Lua's bundled Lua C headers live at `src/lua/lua.h` (lowercase, in subdir). Resident's source uses `#include "lua/lua.h"` (with prefix). The shim `idf_component_register(... INCLUDE_DIRS "src")` exposes `src/`, so `#include <lua.h>` does NOT work — it'd need `INCLUDE_DIRS "src" "src/lua"`. Match resident's convention: prefix with `lua/`.
- PlatformIO `lib_deps` discovery quirks for in-tree libs: `symlink://../../..` (pointing at a parent that contains the project itself) makes PIO stop auto-scanning the project's own `lib/` dir — local libs like `lib/drivers/M5StickDrivers` go missing. Workaround: explicitly add `symlink://lib/drivers` (or each subdir). `lib_extra_dirs = ../../..` scans *children* of the path for libraries, not the path itself, so it doesn't expose the resident root as a library; resident's `library.json` lives at the root and isn't discovered. `file://../../..` works without the extra line but copies rather than links — defeats live edits.
- PlatformIO native test layout: tests MUST live under a `test/` subdir relative to the project (where `platformio.ini` lives). So a project at `test/unit/` puts tests at `test/unit/test/test_smoke/test_smoke.cpp`, not `test/unit/test_smoke/test_smoke.cpp`. PIO ignores test files outside `test/` even though `pio test` will report success-with-no-tests. Clang LSP can't find `unity.h` in test files because PIO downloads it into `.pio/test/<env>/unity/` only when running — the LSP errors are spurious; trust `pio test -e <env>` exit code, not the editor.
- Esp32Lua upstream is `https://github.com/Fischer-Simon/Esp32Lua.git` (capital F-S in URL despite PIO identifier `fischer-simon/Esp32Lua` being lowercase). The repo has NO git tags — only a `main` branch — so `git clone --branch=<tag>` doesn't work; pin by commit SHA via clone-then-checkout. Last known good SHA: `53c7d504ee266532e625145dc141d76692063145` (main as of 2026-04-26).
- resident is a PRIVATE GitHub repo; courier is PUBLIC. CI runs on GitHub Actions don't have an SSH key for resident, so any PIO `lib_deps` line using `git+ssh://...resident.git` will fail in CI. Use `symlink://../../..` (in-tree source) or set up a deploy key.
- `examples/m5stick-claude-code/` is a Cloudflare Worker + Durable Object (`DeviceAgent`) that relays Lua apps over WebSocket to an M5StickC Plus2.
- `POST /agents/device-agent/m5stick-demo` with `text/plain` body = send Lua app; handled in `server/src/server.ts` `onRequest`.
- Lua apps use `init(ctx)` / `on_tick(ctx, dt_ms)` / `on_event(ctx, e)` against `screen.*`, `imu.*`, `buzzer.*` drivers.
- Device ID is hardcoded `m5stick-demo` in `send-app.sh`.
