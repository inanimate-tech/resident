#!/usr/bin/env bash
# Fetches Esp32Lua and ezTime and wraps each as an ESP-IDF component under
# examples/espidf-basic/components/. Neither is on the ESP Component Registry,
# so a pure `idf.py build` can't resolve them.
#
# Upstream repos:
#   - https://github.com/Fischer-Simon/Esp32Lua  (no git tags — pinned by SHA)
#   - https://github.com/ropg/ezTime             (pinned by SHA matching tag 0.8.3)
#
# Bump the SHAs below intentionally when picking up upstream changes.
#
# Re-runnable: skips clone if the target dir exists.

set -euo pipefail

DIR="$(cd "$(dirname "$0")/.." && pwd)"
COMPONENTS="$DIR/components"
mkdir -p "$COMPONENTS"

ESP32LUA_URL="https://github.com/Fischer-Simon/Esp32Lua.git"
ESP32LUA_SHA="53c7d504ee266532e625145dc141d76692063145"

EZTIME_URL="https://github.com/ropg/ezTime.git"
EZTIME_SHA="4797a8f8e70dc62c2cd7e7f5838beb0ab68c32be"

fetch_pinned() {
    local name="$1" url="$2" sha="$3"
    local target="$COMPONENTS/$name"
    if [[ -d "$target" ]]; then
        echo "  $name already present — skipping"
        return
    fi
    echo "  fetching $name @ $sha"
    git clone --quiet "$url" "$target"
    git -C "$target" -c advice.detachedHead=false checkout --quiet "$sha"
    rm -rf "$target/.git"
}

echo "Fetching Esp32Lua into $COMPONENTS"
fetch_pinned Esp32Lua "$ESP32LUA_URL" "$ESP32LUA_SHA"

# Shim CMakeLists for Esp32Lua — upstream ships an Arduino-style library,
# not an IDF component. Glob the Lua C sources, exclude the standalone
# interpreter and compiler entry points, register includes from src/.
cat > "$COMPONENTS/Esp32Lua/CMakeLists.txt" <<'EOF'
file(GLOB LUA_SRCS "src/lua/*.c")
list(FILTER LUA_SRCS EXCLUDE REGEX "lua\\.c$|luac\\.c$")
idf_component_register(SRCS ${LUA_SRCS} INCLUDE_DIRS "src")
EOF

echo "Fetching ezTime into $COMPONENTS"
fetch_pinned ezTime "$EZTIME_URL" "$EZTIME_SHA"

# Shim CMakeLists for ezTime — upstream is an Arduino library. Glob the C++
# sources from src/ and require arduino-esp32 (ezTime uses WiFi, IPAddress,
# String, etc.).
cat > "$COMPONENTS/ezTime/CMakeLists.txt" <<'EOF'
file(GLOB EZTIME_SRCS "src/*.cpp")
idf_component_register(SRCS ${EZTIME_SRCS} INCLUDE_DIRS "src" REQUIRES espressif__arduino-esp32)
EOF

echo "Done."
