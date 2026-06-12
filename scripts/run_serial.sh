#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/build/alchemy_serial"

if [[ ! -x "$BIN" ]]; then
  if [[ -x "$BIN.exe" ]]; then
    BIN="$BIN.exe"
  else
    echo "Missing executable: $BIN" >&2
    echo "Build first with: cmake -S . -B build && cmake --build build" >&2
    exit 1
  fi
fi

mkdir -p "$ROOT_DIR/results"

exec "$BIN" \
  --data "$ROOT_DIR/data/recipes.json" \
  --target "Brick" \
  --algorithm bfs \
  --mode multiple \
  --limit 5 \
  --trace-mode memo \
  --visual-mode shared \
  --output "$ROOT_DIR/results/brick_serial" \
  "$@"
