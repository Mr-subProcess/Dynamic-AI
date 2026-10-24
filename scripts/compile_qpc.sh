#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
if [[ ! -f build/checkpoints/W_sensory.csv ]]; then "$ROOT/scripts/train_pc.sh"; fi
mkdir -p build results
"$ROOT/scripts/generate_qpc_source.sh"
"$ROOT/bin/drnq" qpc-compile "$ROOT/build/Dynamic_AI_QPC.generated.drq" -o "$ROOT/build/Dynamic_AI_QPC.qasm"
"$ROOT/bin/drnq" qpc-report "$ROOT/build/Dynamic_AI_QPC.generated.drq" > "$ROOT/results/qpc_resource_report.txt"
"$ROOT/bin/drnq" qpc-optimize "$ROOT/build/Dynamic_AI_QPC.generated.drq" -o "$ROOT/build/Dynamic_AI_QPC.optimized.qasm" > "$ROOT/results/qpc_optimization.txt"
cp "$ROOT/build/Dynamic_AI_QPC.qasm" "$ROOT/results/Dynamic_AI_QPC.qasm"
cp "$ROOT/build/Dynamic_AI_QPC.optimized.qasm" "$ROOT/results/Dynamic_AI_QPC.optimized.qasm"
echo "Compiled the current learned PC weights into the QPC circuit."
echo "OpenQASM 3: $ROOT/build/Dynamic_AI_QPC.qasm"
echo "Optimized OpenQASM 3: $ROOT/build/Dynamic_AI_QPC.optimized.qasm"
echo "No physical QPU was executed by this command."
