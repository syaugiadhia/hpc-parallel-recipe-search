#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/build/alchemy_mpi"
HOSTFILE=""
NP="4"
ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --hostfile)
      HOSTFILE="${2:-}"
      shift 2
      ;;
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

if [[ -z "$HOSTFILE" ]]; then
  echo "Usage: $0 --hostfile hosts.txt --np 8 [alchemy args]" >&2
  exit 1
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

is_msmpi() {
  command -v mpiexec >/dev/null 2>&1 && mpiexec -help 2>&1 | grep -qi "Microsoft MPI"
}

parse_msmpi_hosts() {
  local hosts=()
  local host slots rest token total_slots
  total_slots=0
  while read -r host rest; do
    [[ -z "${host:-}" || "$host" == \#* ]] && continue
    slots="1"
    for token in $rest; do
      if [[ "$token" == slots=* ]]; then
        slots="${token#slots=}"
      fi
    done
    if [[ ! "$slots" =~ ^[0-9]+$ || "$slots" -lt 1 ]]; then
      echo "Invalid slot count for host '$host': $slots" >&2
      exit 1
    fi
    hosts+=("$host" "$slots")
    total_slots="$((total_slots + slots))"
  done < "$HOSTFILE"

  if [[ "${#hosts[@]}" -eq 0 ]]; then
    echo "Hostfile has no usable hosts: $HOSTFILE" >&2
    exit 1
  fi

  MS_HOST_COUNT="$((${#hosts[@]} / 2))"
  MS_HOST_ARGS=("${hosts[@]}")
  MS_TOTAL_SLOTS="$total_slots"
}

if is_msmpi; then
  parse_msmpi_hosts
  if [[ "$NP" != "$MS_TOTAL_SLOTS" ]]; then
    echo "MS-MPI uses total hostfile slots ($MS_TOTAL_SLOTS) instead of --np $NP" >&2
    NP="$MS_TOTAL_SLOTS"
  fi
  MPI_RUN=(mpiexec -hosts "$MS_HOST_COUNT" "${MS_HOST_ARGS[@]}")
elif command -v mpirun >/dev/null 2>&1; then
  MPI_RUN=(mpirun -np "$NP" --hostfile "$HOSTFILE")
elif command -v mpiexec >/dev/null 2>&1; then
  MPI_RUN=(mpiexec -n "$NP" -machinefile "$HOSTFILE")
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
  --output "$ROOT_DIR/results/brick_mpi_hosts_np$NP" \
  "${ARGS[@]}"
