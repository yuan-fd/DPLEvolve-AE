#!/usr/bin/env bash
# Fresh Table 4 execution: default, BO, and both frozen ReviewDSE tracks.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
THREADS="${THREADS:-10}"
CASE_ID=""

usage() {
  cat <<'EOF'
Usage: artifacts/01-table4-qor/reproduce.sh [--case ID] [--threads N]

Without --case, run the complete nine-case Table 4 experiment, including
3,600 BO placements. With --case, run one case and leave final aggregation
until all nine cases are available.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --case) CASE_ID="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

args=(reproduce-table4 "THREADS=${THREADS}")
if [[ -n "${CASE_ID}" ]]; then args+=("CASE=${CASE_ID}"); fi
exec make -C "${ROOT}" "${args[@]}"
