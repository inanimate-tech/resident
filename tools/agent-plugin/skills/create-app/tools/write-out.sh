#!/usr/bin/env bash
#
# write-out.sh [--out PATH] < lua_source
#
# Reads Lua source from stdin and either writes it to PATH (if --out
# given) or echoes it to stdout. Used by create-app so the agent doesn't
# inline file-writing logic.

set -euo pipefail

out=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) out="$2"; shift 2 ;;
    -h|--help)
      echo "Usage: $0 [--out PATH] < lua_source" >&2
      exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done

if [[ -t 0 ]]; then
  echo "write-out.sh: error: no input on stdin" >&2
  exit 2
fi

if [[ -n "$out" ]]; then
  mkdir -p "$(dirname "$out")"
  cat > "$out"
  echo "Wrote $out" >&2
else
  cat
fi
