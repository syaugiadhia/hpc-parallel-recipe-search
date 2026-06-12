#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERIAL_BIN="$ROOT_DIR/build/alchemy_serial"
MPI_BIN="$ROOT_DIR/build/alchemy_mpi"
TARGETS="$ROOT_DIR/benchmarks/targets.txt"
DATA="$ROOT_DIR/data/recipes.json"
RESULTS="$ROOT_DIR/results"

if [[ ! -x "$SERIAL_BIN" ]]; then
  if [[ -x "$SERIAL_BIN.exe" ]]; then
    SERIAL_BIN="$SERIAL_BIN.exe"
  else
    echo "Missing executable: $SERIAL_BIN" >&2
    echo "Build first with: cmake -S . -B build && cmake --build build" >&2
    exit 1
  fi
fi

mkdir -p "$RESULTS"

"$SERIAL_BIN" --benchmark "$TARGETS" --data "$DATA" --algorithm bfs --mode multiple --limit 10 --trace-mode full --visual-mode full --output "$RESULTS/bench_serial_bfs_full_full"
"$SERIAL_BIN" --benchmark "$TARGETS" --data "$DATA" --algorithm bfs --mode multiple --limit 10 --trace-mode memo --visual-mode full --output "$RESULTS/bench_serial_bfs_memo_full"
"$SERIAL_BIN" --benchmark "$TARGETS" --data "$DATA" --algorithm bfs --mode multiple --limit 10 --trace-mode memo --visual-mode shared --output "$RESULTS/bench_serial_bfs_memo_shared"
"$SERIAL_BIN" --benchmark "$TARGETS" --data "$DATA" --algorithm dfs --mode multiple --limit 10 --trace-mode full --visual-mode full --output "$RESULTS/bench_serial_dfs_full_full"

if [[ ! -x "$MPI_BIN" && -x "$MPI_BIN.exe" ]]; then
  MPI_BIN="$MPI_BIN.exe"
fi

if [[ -x "$MPI_BIN" ]]; then
  if command -v mpirun >/dev/null 2>&1; then
    MPI_LAUNCH=(mpirun -np)
  elif command -v mpiexec >/dev/null 2>&1; then
    MPI_LAUNCH=(mpiexec -n)
  else
    echo "Skipping MPI benchmark: mpirun/mpiexec not found" >&2
    exit 0
  fi

  if command -v nproc >/dev/null 2>&1; then
    CORES="$(nproc)"
  else
    CORES="4"
  fi

  for NP in 2 4 8; do
    if [[ "$NP" -le "$CORES" ]]; then
      "${MPI_LAUNCH[@]}" "$NP" "$MPI_BIN" --benchmark "$TARGETS" --data "$DATA" --algorithm bfs --mode multiple --limit 10 --trace-mode memo --visual-mode shared --split-depth 2 --output "$RESULTS/bench_mpi_np$NP"
    fi
  done
else
  echo "Skipping MPI benchmark: $MPI_BIN was not built" >&2
fi
