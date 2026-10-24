#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
"$ROOT/scripts/test_compiler.sh"
"$ROOT/scripts/train_pc.sh"
"$ROOT/scripts/run_online.sh"
"$ROOT/scripts/run_inference.sh"
"$ROOT/scripts/compile_qpc.sh"
"$ROOT/scripts/verify.sh"
printf "\nDynamic AI end-to-end workflow completed. Physical QPU execution is not implied.\n"
