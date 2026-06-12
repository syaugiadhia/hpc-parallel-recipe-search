#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/build/alchemy_mpi"
NP="auto"
ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --np)
      NP="${2:-}"
      shift 2
      ;;
    *)
      ARGS+=("$1")
      shift
      ;;
  esac
done

if [[ "$NP" == "auto" ]]; then
  if command -v nproc >/dev/null 2>&1; then
    NP="$(nproc)"
  else
    NP="4"
  fi
fi

if [[ ! -x "$BIN" ]]; then
  if [[ -x "$BIN.exe" ]]; then
    BIN="$BIN.exe"
  else
    echo "Missing executable: $BIN" >&2
    echo "Build first with: cmake -S . -B build && cmake --build build" >&2
    exit 1
  fi
fi

if command -v mpirun >/dev/null 2>&1; then
  MPI_RUN=(mpirun -np "$NP")
elif command -v mpiexec >/dev/null 2>&1; then
  MPI_RUN=(mpiexec -n "$NP")
else
  echo "mpirun/mpiexec was not found" >&2
  exit 1
fi

mkdir -p "$ROOT_DIR/results"

exec "${MPI_RUN[@]}" "$BIN" \
  --data "$ROOT_DIR/data/recipes.json" \
  --target "Brick" \
  --algorithm bfs \
  --mode multiple \
  --limit 10 \
  --trace-mode memo \
  --visual-mode shared \
  --split-depth 2 \
  --output "$ROOT_DIR/results/brick_mpi_local_np$NP" \
  "${ARGS[@]}"
