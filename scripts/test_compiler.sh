#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/common.sh"
TESTS="$ROOT/vendor/DRNC-Q-v1.8/tests/run_tests.sh"
[[ -x "$TESTS" ]] || { echo "Bundled DRNC-Q regression suite not found: $TESTS" >&2; exit 127; }
mkdir -p "$ROOT/results"
BIN="$DRNQ_BIN" bash "$TESTS" | tee "$ROOT/results/compiler_regression.txt"
