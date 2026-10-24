#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
CKPT="build/checkpoints"
TEMPLATE="templates/Dynamic_AI_QPC.drq.in"
[[ -f "$CKPT/W_sensory.csv" && -f "$CKPT/W_memory.csv" && -f "$CKPT/W_branch.csv" && -f "$CKPT/W_output.csv" ]] || {
  echo "Model checkpoints are missing; run ./scripts/train_pc.sh first." >&2; exit 2;
}
value_at() { awk -F, -v idx="$2" 'NR==1 {gsub(/^[ \t]+|[ \t]+$/, "", $idx); printf "%.10f", $idx}' "$1"; }
# Row-major indices 1, 5, 9 are the three diagonal weights of each 3x3 learned matrix.
WS00="$(value_at "$CKPT/W_sensory.csv" 1)"
WS11="$(value_at "$CKPT/W_sensory.csv" 5)"
WS22="$(value_at "$CKPT/W_sensory.csv" 9)"
WM00="$(value_at "$CKPT/W_memory.csv" 1)"
WM11="$(value_at "$CKPT/W_memory.csv" 5)"
WM22="$(value_at "$CKPT/W_memory.csv" 9)"
WB00="$(value_at "$CKPT/W_branch.csv" 1)"
WB11="$(value_at "$CKPT/W_branch.csv" 5)"
WB22="$(value_at "$CKPT/W_branch.csv" 9)"
WO00="$(value_at "$CKPT/W_output.csv" 1)"
WO11="$(value_at "$CKPT/W_output.csv" 5)"
WO22="$(value_at "$CKPT/W_output.csv" 9)"
mkdir -p build
sed \
  -e "s/@WS00@/$WS00/g" -e "s/@WS11@/$WS11/g" -e "s/@WS22@/$WS22/g" \
  -e "s/@WM00@/$WM00/g" -e "s/@WM11@/$WM11/g" -e "s/@WM22@/$WM22/g" \
  -e "s/@WB00@/$WB00/g" -e "s/@WB11@/$WB11/g" -e "s/@WB22@/$WB22/g" \
  -e "s/@WO00@/$WO00/g" -e "s/@WO11@/$WO11/g" -e "s/@WO22@/$WO22/g" \
  "$TEMPLATE" > build/Dynamic_AI_QPC.generated.drq
if grep -Eq '@(WS|WM|WB|WO)[0-9]{2}@' build/Dynamic_AI_QPC.generated.drq; then
  echo "Unsubstituted trainable-angle token remains in generated QPC source." >&2; exit 3
fi
"$ROOT/bin/drnq" qpc-check build/Dynamic_AI_QPC.generated.drq
cp build/Dynamic_AI_QPC.generated.drq results/Dynamic_AI_QPC.generated.drq
printf 'Generated QPC source from the current PC checkpoint: %s\n' "$ROOT/build/Dynamic_AI_QPC.generated.drq"
