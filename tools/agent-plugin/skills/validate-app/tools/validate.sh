#!/usr/bin/env bash
#
# validate.sh — validate a Resident Lua app locally.
#
# Usage:
#   validate.sh path/to/app.lua
#   cat app.lua | validate.sh
#   validate.sh --device-skill path/to/DEVICE-SKILL.md path/to/app.lua
#
# Exit codes:
#   0  — passed
#   1  — validation failed (line written to stderr)
#   2  — environment error (lua missing, file not found, etc.)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILTINS="$SCRIPT_DIR/builtins.lua"
HARNESS="$SCRIPT_DIR/harness.lua"
DEDUCE="$SCRIPT_DIR/deduce-modules.sh"

device_skill=""
ref_files=()
app_file=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --device-skill) device_skill="$2"; shift 2 ;;
    --ref)          ref_files+=("$2"); shift 2 ;;
    -h|--help)
      cat <<EOF
Usage: $0 [--device-skill PATH] [--ref PATH ...] [APP_FILE]
       cat app.lua | $0 [--device-skill PATH] [--ref PATH ...]

Validates a Resident Lua app locally. Reads from APP_FILE or stdin.
--ref may be passed multiple times. Each ref file is processed the
same way as DEVICE-SKILL.md: module names are auto-deduced from
\`\`\`lua blocks, and a \`## Validation stubs\` Lua block (if any) is
appended after auto-deduced stubs. Plain .lua refs are silently
ignored (no Markdown structure to extract).
EOF
      exit 0
      ;;
    -*) echo "Unknown option: $1" >&2; exit 2 ;;
    *)  app_file="$1"; shift ;;
  esac
done

if ! command -v lua >/dev/null 2>&1; then
  echo "validate-app: error: 'lua' not found on PATH." >&2
  echo "Install with: brew install lua" >&2
  exit 2
fi

if [[ -z "$device_skill" && -f "./DEVICE-SKILL.md" ]]; then
  device_skill="./DEVICE-SKILL.md"
fi

if [[ -n "$app_file" ]]; then
  if [[ ! -f "$app_file" ]]; then
    echo "validate-app: error: file not found: $app_file" >&2
    exit 2
  fi
  app_code=$(cat "$app_file")
elif [[ ! -t 0 ]]; then
  app_code=$(cat)
else
  echo "validate-app: error: no app provided. Pass a file path or pipe via stdin." >&2
  exit 2
fi

# Build stubs by processing each input file (DEVICE-SKILL.md first, then
# each --ref in argv order). For each file:
#   1. Deduce module identifiers from ```lua blocks → permissive metatables.
#   2. Extract `## Validation stubs` Lua block → concrete getter returns.
# Later writes override earlier ones at Lua runtime.
device_stubs=""
validation_stubs=""

process_stubs_file() {
  local file="$1"
  [[ -z "$file" || ! -f "$file" ]] && return 0
  while IFS= read -r module; do
    [[ -z "$module" ]] && continue
    device_stubs+=$'\n'"$module = setmetatable({}, { __index = function() return function() end end })"
  done < <("$DEDUCE" "$file")
  local extracted
  extracted=$(awk '
    /^## Validation stubs/ { in_section = 1; next }
    in_section && /^## /   { in_section = 0; next }
    in_section && /^```lua/ { in_block = 1; next }
    in_section && /^```/    { in_block = 0; next }
    in_section && in_block  { print }
  ' "$file")
  if [[ -n "$extracted" ]]; then
    validation_stubs+=$'\n'"$extracted"
  fi
}

process_stubs_file "$device_skill"
for ref in ${ref_files[@]+"${ref_files[@]}"}; do
  process_stubs_file "$ref"
done

# A catch-all metatable on _G so any unknown global access also gets a no-op.
# Apps that reference truly unknown things won't crash on simple .x access.
fallback_stub='
setmetatable(_G, { __index = function(_, _) return setmetatable({}, { __index = function() return function() end end }) end })
'

# Compose harness via a temp file (preserves line numbers in errors poorly,
# but is robust to embedded newlines / quoting).
tmp=$(mktemp -t validate-app.XXXXXX.lua)
trap 'rm -f "$tmp"' EXIT

{
  cat "$BUILTINS"
  echo "$device_stubs"
  echo "$validation_stubs"
  echo "$fallback_stub"
  echo '-- ===== USER APP ====='
  echo "$app_code"
  echo '-- ===== HARNESS SETUP ====='
  echo 'setmetatable(_G, nil) -- remove catch-all so harness nil-checks work'
  echo '-- ===== HARNESS ====='
  cat "$HARNESS"
} > "$tmp"

# Run; capture stderr.
if err=$(lua "$tmp" 2>&1 >/dev/null); then
  echo "validate-app: OK"
  exit 0
fi

# `lua` exited non-zero. The harness writes its own structured FAIL line on
# its own errors; if we got here without that prefix, it's likely a Lua-level
# load/compile error. Print whatever stderr we got.
if [[ -n "$err" ]]; then
  echo "$err" >&2
else
  echo "validate-app: FAIL: lua exited non-zero with no stderr" >&2
fi
exit 1
