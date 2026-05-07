#!/usr/bin/env bash
#
# send-app.sh — Push a Lua app to a Resident device via the canonical relay.
#
# Reads the device's ID off the device's screen, then runs:
#   ./send-app.sh --device-id <id> device-apps/hello.lua
#   cat device-apps/hello.lua | ./send-app.sh --device-id <id>
#
# Defaults to the public relay at https://resident.inanimate.tech.
# Pass --base-url to target a self-hosted Worker (e.g. http://localhost:5173
# while running `npm run dev` from server/).
#
# The device ID can also come from the RESIDENT_DEVICE_ID env var or a
# .resident-device-id file in cwd, so you don't have to repeat the flag.
#
# Requires: curl, jq.

set -euo pipefail

PROD_URL="https://resident.inanimate.tech"
DEV_URL="http://localhost:5173"

base_url="$PROD_URL"
device_id="${RESIDENT_DEVICE_ID:-}"
app_file=""

usage() {
  cat <<EOF >&2
Usage: $0 [--base-url URL | --dev] [--device-id ID] [APP_FILE]
       cat app.lua | $0 [...flags]

Defaults:
  --base-url   $PROD_URL
  --device-id  \$RESIDENT_DEVICE_ID, then ./.resident-device-id, then required

Flags:
  --base-url URL     Push to URL/devices/<id>/send
  --dev              Shortcut for --base-url $DEV_URL
  --device-id ID     8-char hex device ID (read off the device screen)
  -h, --help         This help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --base-url)  base_url="$2"; shift 2 ;;
    --dev)       base_url="$DEV_URL"; shift ;;
    --device-id) device_id="$2"; shift 2 ;;
    -h|--help)   usage; exit 0 ;;
    -*)          echo "Unknown option: $1" >&2; usage; exit 2 ;;
    *)           app_file="$1"; shift ;;
  esac
done

# Fall back to .resident-device-id in cwd if still unset.
if [[ -z "$device_id" && -f .resident-device-id ]]; then
  device_id=$(tr -d '[:space:]' < .resident-device-id)
fi

if [[ -z "$device_id" ]]; then
  echo "send-app: error: device ID required." >&2
  echo "  Read it off the device screen, then either:" >&2
  echo "    pass --device-id <id>, or" >&2
  echo "    set RESIDENT_DEVICE_ID=<id> in your shell, or" >&2
  echo "    write it into ./.resident-device-id" >&2
  exit 2
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "send-app: error: 'jq' not found. Install with: brew install jq" >&2
  exit 2
fi

# Read source.
if [[ -n "$app_file" ]]; then
  if [[ ! -f "$app_file" ]]; then
    echo "send-app: error: file not found: $app_file" >&2
    exit 2
  fi
  code=$(cat "$app_file")
  app_label="$app_file"
elif [[ ! -t 0 ]]; then
  code=$(cat)
  app_label="stdin"
else
  echo "send-app: error: no app provided. Pass a file path or pipe via stdin." >&2
  usage
  exit 2
fi

if [[ -z "$code" ]]; then
  echo "send-app: error: app source is empty" >&2
  exit 2
fi

payload=$(jq -n --arg code "$code" '{type: "app", code: $code}')
endpoint="${base_url}/devices/${device_id}/send"

echo "Sending $app_label to $endpoint" >&2

tmp_body=$(mktemp)
trap 'rm -f "$tmp_body"' EXIT

http_code=$(curl -sS -o "$tmp_body" -w "%{http_code}" \
  -X POST \
  -H "Content-Type: application/json" \
  --data-binary "$payload" \
  "$endpoint")

case "$http_code" in
  200)
    echo "Sent." >&2
    exit 0
    ;;
  503)
    echo "send-app: device not connected." >&2
    echo "  Check the device is powered, on Wi-Fi, and the deviceId on" >&2
    echo "  the screen matches '$device_id'." >&2
    exit 1
    ;;
  *)
    echo "send-app: HTTP $http_code from $endpoint" >&2
    cat "$tmp_body" >&2
    echo >&2
    exit 3
    ;;
esac
