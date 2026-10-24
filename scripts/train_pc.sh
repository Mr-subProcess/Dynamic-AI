#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p build/checkpoints results
"$ROOT/bin/drnq" check "$ROOT/Dynamic_AI_PC.drq"
"$ROOT/bin/drnq" run "$ROOT/Dynamic_AI_PC.drq" | tee "$ROOT/results/training_console.log"
