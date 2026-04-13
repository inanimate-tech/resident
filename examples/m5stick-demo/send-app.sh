#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BASE_URL=""

# Parse flags
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dev-url)
      BASE_URL="$2"
      shift 2
      ;;
    -*)
      echo "Unknown flag: $1" >&2
      echo "Usage: $0 [--dev-url URL] <app-file.lua>" >&2
      exit 1
      ;;
    *)
      break
      ;;
  esac
done

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 [--dev-url URL] <app-file.lua>" >&2
  exit 1
fi

DEVICE_ID="m5stick-demo"
APP_FILE="$1"

if [[ ! -f "$APP_FILE" ]]; then
  echo "Error: file not found: $APP_FILE" >&2
  exit 1
fi

# Resolve production URL from wrangler if not overridden
if [[ -z "$BASE_URL" ]]; then
  WORKER_NAME=$(cd "$SCRIPT_DIR/server" && npx --yes wrangler deployments list --json 2>/dev/null | \
    python3 -c "import sys,json; items=json.load(sys.stdin)['items']; print(items[0]['url'])" 2>/dev/null || true)

  if [[ -z "$WORKER_NAME" ]]; then
    # Fallback: parse worker name from wrangler.jsonc
    NAME=$(cd "$SCRIPT_DIR/server" && python3 -c "
import json, re
with open('wrangler.jsonc') as f:
    text = re.sub(r'//.*', '', f.read())
    print(json.loads(text)['name'])
")
    BASE_URL="https://${NAME}.workers.dev"
    echo "Using derived URL: $BASE_URL"
  else
    BASE_URL="https://$WORKER_NAME"
    echo "Using deployed URL: $BASE_URL"
  fi
fi

URL="${BASE_URL}/agents/device-agent/${DEVICE_ID}"
echo "Sending $(basename "$APP_FILE") to $URL"

RESPONSE=$(curl -s -w "\n%{http_code}" \
  -X POST \
  -H "Content-Type: text/plain" \
  --data-binary "@${APP_FILE}" \
  "$URL")

HTTP_CODE=$(echo "$RESPONSE" | tail -1)
BODY=$(echo "$RESPONSE" | sed '$d')

if [[ -n "$BODY" ]]; then
  echo "$BODY"
fi

if [[ "$HTTP_CODE" == "200" ]]; then
  echo "Sent successfully."
else
  echo "Failed with HTTP $HTTP_CODE" >&2
  exit 1
fi
