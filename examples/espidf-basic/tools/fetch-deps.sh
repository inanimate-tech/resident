#!/usr/bin/env bash
# Fetches Esp32Lua and wraps it as an ESP-IDF component under
# examples/espidf-basic/components/. Esp32Lua is not on the ESP Component
# Registry, so a pure `idf.py build` can't resolve it.
#
# The upstream repo (https://github.com/Fischer-Simon/Esp32Lua) has no git
# tags, only a main branch — so we pin by commit SHA via clone-then-checkout.
# Bump the SHA below intentionally when picking up upstream changes.
#
# Re-runnable: skips clone if the target dir exists.

set -euo pipefail

DIR="$(cd "$(dirname "$0")/.." && pwd)"
COMPONENTS="$DIR/components"
mkdir -p "$COMPONENTS"

ESP32LUA_URL="https://github.com/Fischer-Simon/Esp32Lua.git"
ESP32LUA_SHA="53c7d504ee266532e625145dc141d76692063145"

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

echo "Done."
