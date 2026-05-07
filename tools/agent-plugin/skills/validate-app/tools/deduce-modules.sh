#!/usr/bin/env bash
#
# deduce-modules.sh DEVICE-SKILL.md
#   Reads DEVICE-SKILL.md, extracts top-level identifiers used as
#   <ident>.<member> or <ident>(<args>) inside ```lua code blocks, and
#   emits the unique device-specific module names on stdout, one per line.
#
# Filters out sandbox built-ins (which are stubbed by builtins.lua) and
# Lua built-ins (which the real interpreter provides).

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 DEVICE-SKILL.md" >&2
  exit 2
fi

src="$1"
if [[ ! -f "$src" ]]; then
  echo "Error: file not found: $src" >&2
  exit 2
fi

# Identifiers we never emit (already provided by builtins.lua or Lua itself).
exclude='^(log|time|kv|rgb|fract|beat|noise2d|floor|ceil|abs|sin|cos|tan|sqrt|min|max|fmod|math|string|table|io|os|coroutine|debug|package|tostring|tonumber|type|pairs|ipairs|next|select|error|assert|pcall|xpcall|setmetatable|getmetatable|rawset|rawget|rawequal|rawlen|require|print|unpack|_G|_VERSION|init|on_tick|on_event|ctx|dt_ms|e|event|self|true|false|nil|local|function|end|if|then|else|elseif|for|do|while|repeat|until|break|return|in|and|or|not)$'

awk '
  /^```lua/ { in_block = 1; next }
  /^```/    { in_block = 0; next }
  in_block  { print }
' "$src" \
  | grep -oE '\b[a-zA-Z_][a-zA-Z0-9_]*\b(\.|[(])' \
  | sed 's/[.(]$//' \
  | sort -u \
  | grep -Ev "$exclude" || true
