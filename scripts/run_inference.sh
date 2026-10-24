#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
if [[ ! -f build/checkpoints/W_sensory.csv ]]; then
  echo "Checkpoint missing; run ./scripts/train_pc.sh first." >&2
  exit 2
fi
mkdir -p results
"$ROOT/bin/drnq" check "$ROOT/Dynamic_AI_Infer_PC.drq"
"$ROOT/bin/drnq" run "$ROOT/Dynamic_AI_Infer_PC.drq" | tee "$ROOT/results/inference_console.log"
