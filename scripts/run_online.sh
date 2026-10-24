#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
[[ -f build/checkpoints/W_sensory.csv ]] || { echo "Checkpoint missing; run ./scripts/train_pc.sh first." >&2; exit 2; }
mkdir -p results
BEFORE="$(sha256sum build/checkpoints/W_output.csv | awk '{print $1}')"
"$ROOT/bin/drnq" check "$ROOT/Dynamic_AI_Online_PC.drq"
"$ROOT/bin/drnq" run "$ROOT/Dynamic_AI_Online_PC.drq" | tee "$ROOT/results/online_console.log"
AFTER="$(sha256sum build/checkpoints/W_output.csv | awk '{print $1}')"
[[ "$BEFORE" != "$AFTER" ]] || { echo "Online update did not modify output weights." >&2; exit 3; }
printf 'Checkpoint update verified: W_output.csv changed after labeled feedback.\n'
