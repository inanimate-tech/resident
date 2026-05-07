#!/usr/bin/env bash
#
# push.sh — POST a Lua app to a Resident relay.
#
# Usage:
#   push.sh --base-url <url> --device-id <id> [APP_FILE]
#   cat app.lua | push.sh --base-url <url> --device-id <id>

set -euo pipefail

base_url=""
device_id=""
app_file=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --base-url)  base_url="$2";  shift 2 ;;
    --device-id) device_id="$2"; shift 2 ;;
    -h|--help)
      cat <<EOF
Usage: $0 --base-url <url> --device-id <id> [APP_FILE]
       cat app.lua | $0 --base-url <url> --device-id <id>
EOF
      exit 0 ;;
    -*) echo "Unknown option: $1" >&2; exit 2 ;;
    *)  app_file="$1"; shift ;;
  esac
done

if [[ -z "$base_url" ]]; then
  echo "push-app: error: --base-url is required" >&2; exit 2
fi
if [[ -z "$device_id" ]]; then
  echo "push-app: error: --device-id is required" >&2; exit 2
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "push-app: error: 'jq' not found. Install with: brew install jq" >&2
  exit 2
fi

# Read source from file or stdin.
if [[ -n "$app_file" ]]; then
  if [[ ! -f "$app_file" ]]; then
    echo "push-app: error: file not found: $app_file" >&2; exit 2
  fi
  code=$(cat "$app_file")
elif [[ ! -t 0 ]]; then
  code=$(cat)
else
  echo "push-app: error: no app provided. Pass a file path or pipe via stdin." >&2
  exit 2
fi

if [[ -z "$code" ]]; then
  echo "push-app: error: app source is empty" >&2; exit 2
fi

# Build the JSON envelope.
payload=$(jq -n --arg code "$code" '{type: "app", code: $code}')

endpoint="${base_url}/devices/${device_id}/send"

# POST and capture both body and status.
tmp_body=$(mktemp)
trap 'rm -f "$tmp_body"' EXIT

http_code=$(curl -sS -o "$tmp_body" -w "%{http_code}" \
  -X POST \
  -H "Content-Type: application/json" \
  --data-binary "$payload" \
  "$endpoint")

case "$http_code" in
  200)
    echo "push-app: sent to $endpoint" >&2
    exit 0
    ;;
  503)
    echo "push-app: device not connected ($endpoint)" >&2
    exit 1
    ;;
  400|415|404)
    echo "push-app: error: HTTP $http_code from $endpoint" >&2
    cat "$tmp_body" >&2
    exit 3
    ;;
  *)
    echo "push-app: error: HTTP $http_code from $endpoint" >&2
    cat "$tmp_body" >&2
    exit 3
    ;;
esac
