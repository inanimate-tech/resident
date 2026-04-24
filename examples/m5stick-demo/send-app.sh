#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROD_URL="https://outrun-m5stick-demo.genmon.workers.dev"
DEV_URL="http://localhost:5173"
BASE_URL=""

# Parse flags
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dev)
      BASE_URL="$DEV_URL"
      shift
      ;;
    -*)
      echo "Unknown flag: $1" >&2
      echo "Usage: $0 [--dev] [app-file.lua]" >&2
      echo "       cat app.lua | $0 [--dev]" >&2
      exit 1
      ;;
    *)
      break
      ;;
  esac
done

DEVICE_ID="m5stick-demo"

# Read app code from file argument or stdin
if [[ $# -ge 1 ]]; then
  APP_FILE="$1"
  if [[ ! -f "$APP_FILE" ]]; then
    echo "Error: file not found: $APP_FILE" >&2
    exit 1
  fi
  APP_CODE=$(cat "$APP_FILE")
  APP_NAME=$(basename "$APP_FILE")
elif [[ ! -t 0 ]]; then
  APP_CODE=$(cat)
  APP_NAME="stdin"
else
  echo "Usage: $0 [--dev] [app-file.lua]" >&2
  echo "       cat app.lua | $0 [--dev]" >&2
  exit 1
fi

if [[ -z "$BASE_URL" ]]; then
  BASE_URL="$PROD_URL"
fi

URL="${BASE_URL}/agents/device-agent/${DEVICE_ID}"
echo "Sending ${APP_NAME} to $URL"

RESPONSE=$(curl -s -w "\n%{http_code}" \
  -X POST \
  -H "Content-Type: text/plain" \
  --data-binary "$APP_CODE" \
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
