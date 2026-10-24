#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
DRIVER="${DRNC_QPC_DRIVER:-}"
SHOTS="${SHOTS:-1024}"
if [[ -z "$DRIVER" ]]; then
  echo "No physical QPU driver configured." >&2
  echo "Set DRNC_QPC_DRIVER to an executable implementing vendor/DRNC-Q-v1.8/hardware/LOCAL_QPU_DRIVER_ABI.md." >&2
  echo "QPC compilation is not the same as physical hardware execution." >&2
  exit 2
fi
[[ -x "$DRIVER" ]] || { echo "Driver is not executable: $DRIVER" >&2; exit 127; }
"$ROOT/scripts/compile_qpc.sh"
exec "$ROOT/bin/drnq" qpc-run "$ROOT/build/Dynamic_AI_QPC.generated.drq" --driver "$DRIVER" --shots "$SHOTS" --optimize
