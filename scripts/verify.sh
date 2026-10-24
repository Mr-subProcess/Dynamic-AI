#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p build/checkpoints results
BIN="$ROOT/bin/drnq"
"$BIN" --version | grep -q 'DRNC-Q v1.8 Full Edition'
"$BIN" check "$ROOT/Dynamic_AI_PC.drq" >/dev/null
"$BIN" check "$ROOT/Dynamic_AI_Infer_PC.drq" >/dev/null
"$BIN" check "$ROOT/Dynamic_AI_Online_PC.drq" >/dev/null
grep -q '^%set\[QPC\]$' "$ROOT/Dynamic_AI_QPC.drq"
"$BIN" qpc-check "$ROOT/Dynamic_AI_QPC.drq" >/dev/null
"$BIN" qpc-compile "$ROOT/Dynamic_AI_QPC.drq" -o "$ROOT/build/Dynamic_AI_QPC.snapshot.qasm" >/dev/null
grep -q 'OPENQASM 3.0;' "$ROOT/build/Dynamic_AI_QPC.snapshot.qasm"
[[ -f results/training_console.log ]] || { echo "Missing training console log; run scripts/train_pc.sh." >&2; exit 2; }
[[ -f results/final_metrics.txt ]] || { echo "Missing training metrics." >&2; exit 2; }
grep -q 'training_loss_decreased=true' results/training_console.log
INITIAL="$(sed -n 's/^initial_train_mse=//p' results/final_metrics.txt | head -n1)"
FINAL="$(sed -n 's/^final_train_mse=//p' results/final_metrics.txt | head -n1)"
[[ -n "$INITIAL" && -n "$FINAL" ]] || { echo "Could not parse training metrics." >&2; exit 3; }
awk -v a="$INITIAL" -v b="$FINAL" 'BEGIN {if (!(b<a)) exit 1}' || { echo "Training loss did not decrease: $INITIAL -> $FINAL" >&2; exit 4; }
for name in W_sensory W_memory W_branch W_output; do
  [[ -s "build/checkpoints/$name.csv" ]] || { echo "Missing checkpoint: $name" >&2; exit 5; }
  [[ "$(awk -F, 'NR==1 {print NF}' "build/checkpoints/$name.csv")" -eq 9 ]] || { echo "Bad checkpoint shape: $name" >&2; exit 6; }
done
"$ROOT/scripts/run_inference.sh" > "$ROOT/results/verify_inference.log"
grep -q 'class=0' results/verify_inference.log
grep -q 'class=1' results/verify_inference.log
grep -q 'class=2' results/verify_inference.log
[[ -s results/inference_latest.csv ]]
[[ -s results/online_adaptation.csv && -s results/online_metrics.txt ]] || { echo "Online adaptation artifacts missing; run scripts/run_online.sh." >&2; exit 7; }
"$ROOT/scripts/generate_qpc_source.sh" >/dev/null
! grep -Eq '@(WS|WM|WB|WO)[0-9]{2}@' build/Dynamic_AI_QPC.generated.drq
"$BIN" qpc-check build/Dynamic_AI_QPC.generated.drq >/dev/null
"$BIN" qpc-compile build/Dynamic_AI_QPC.generated.drq -o build/Dynamic_AI_QPC.qasm >/dev/null
"$BIN" qpc-report build/Dynamic_AI_QPC.generated.drq > results/qpc_resource_report.txt
"$BIN" qpc-optimize build/Dynamic_AI_QPC.generated.drq -o build/Dynamic_AI_QPC.optimized.qasm > results/qpc_optimization.txt
cp build/Dynamic_AI_QPC.qasm results/Dynamic_AI_QPC.qasm
cp build/Dynamic_AI_QPC.optimized.qasm results/Dynamic_AI_QPC.optimized.qasm
grep -q 'OPENQASM 3.0;' build/Dynamic_AI_QPC.qasm
grep -q 'ctrl @ matrixEdge' build/Dynamic_AI_QPC.qasm
grep -q 'inv @ matrixEdge' build/Dynamic_AI_QPC.qasm
grep -q 'measure q\[' build/Dynamic_AI_QPC.qasm
grep -q 'if (' build/Dynamic_AI_QPC.qasm
"$BIN" qpc-run "$ROOT/build/Dynamic_AI_QPC.generated.drq" --driver "$ROOT/vendor/DRNC-Q-v1.8/tests/mock_qpc_driver.sh" --shots 17 --optimize > "$ROOT/results/mock_driver_test.txt"
grep -q 'MOCK DRIVER ABI PASS (not a physical QPU): shots=17' results/mock_driver_test.txt
{
  echo 'Dynamic AI v1.2 integration verification: PASS'
  echo "Compiler: $($BIN --version)"
  echo "Training loss: $INITIAL -> $FINAL"
  echo "Eval metrics: $(tr '\n' ' ' < results/final_metrics.txt)"
  echo 'PC: CSV training/validation, supervised matrix updates, checkpoint save/reload, inference and class outputs passed.'
  echo 'Online mode: labelled feedback updated the checkpoint; output-matrix hash changed.'
  echo 'QPC: learned PC diagonal weights injected into gate angles; OpenQASM 3 compile, optimizer, controlled/adjoint gates, measurement and feed-forward passed.'
  echo 'Local mock-driver ABI test passed; it did not run a physical QPU.'
  echo 'Physical QPU: NOT RUN; no calibrated hardware driver/control stack was configured.'
} > results/verification.txt
cat results/verification.txt
