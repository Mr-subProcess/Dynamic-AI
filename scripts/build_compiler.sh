#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
make -C "$ROOT/vendor/DRNC-Q-v1.8" clean all
"$ROOT/bin/drnq" --version
