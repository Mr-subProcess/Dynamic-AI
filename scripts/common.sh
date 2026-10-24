#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -n "${DRNQ_BIN:-}" ]]; then
  if [[ "$DRNQ_BIN" != */* ]]; then
    command -v "$DRNQ_BIN" >/dev/null 2>&1 || { echo "DRNQ_BIN not found in PATH: $DRNQ_BIN" >&2; exit 127; }
    DRNQ_BIN="$(command -v "$DRNQ_BIN")"
  fi
else
  DRNQ_BIN="$ROOT/bin/drnq"
fi
if [[ ! -x "$DRNQ_BIN" ]]; then
  echo "Bundled DRNC-Q compiler is missing or not executable: $DRNQ_BIN" >&2
  echo "Restore bin/drnq from the complete Dynamic AI bundle or set DRNQ_BIN to an authorized installation." >&2
  exit 127
fi
export ROOT DRNQ_BIN
