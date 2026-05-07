#!/usr/bin/env bash
#
# Run validate-app against each fixture. Pass-fixtures must exit 0,
# fail-fixtures must exit 1.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FIXTURES="$SCRIPT_DIR/fixtures"
VALIDATE="$SCRIPT_DIR/../tools/validate.sh"
SKILL="$FIXTURES/DEVICE-SKILL.md"

fail=0
pass=0

run() {
  local fixture="$1"
  local expected="$2"
  local out err code
  out=$("$VALIDATE" --device-skill "$SKILL" "$fixture" 2>&1)
  code=$?
  if [[ "$expected" == "ok" && "$code" -eq 0 ]]; then
    echo "PASS  $fixture"
    pass=$((pass+1))
  elif [[ "$expected" == "fail" && "$code" -eq 1 ]]; then
    echo "PASS  $fixture (expected fail; exit 1)"
    pass=$((pass+1))
  else
    echo "FAIL  $fixture (expected=$expected, got code=$code)"
    echo "      output: $out"
    fail=$((fail+1))
  fi
}

run "$FIXTURES/ok-init-only.lua"      "ok"
run "$FIXTURES/ok-bouncing-ball.lua"  "ok"
run "$FIXTURES/fail-no-lifecycle.lua" "fail"
run "$FIXTURES/fail-syntax-error.lua" "fail"
run "$FIXTURES/fail-runtime-error.lua" "fail"

echo
echo "$pass passed, $fail failed"
exit $fail
