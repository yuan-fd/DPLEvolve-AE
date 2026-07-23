#!/usr/bin/env bash
# Fresh Table 6 execution from retained cut-row DEF/Verilog/SDC data.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
THREADS="${THREADS:-10}"
FETCH=0
CASE_ID=""
PATTERN=""
ROLE=""

usage() {
  cat <<'EOF'
Usage: artifacts/03-table6-cutrow/reproduce.sh [options]

Options:
  --fetch          Download and verify the published Table 6 data first.
  --case ID        Run one case.
  --pattern ID     Run one cut-row pattern; requires --case.
  --role ROLE      Run diamond, negotiation, or reviewdse only.
  --threads N      OpenROAD threads. Default: 10.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --fetch) FETCH=1; shift ;;
    --case) CASE_ID="$2"; shift 2 ;;
    --pattern) PATTERN="$2"; shift 2 ;;
    --role) ROLE="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -n "${PATTERN}" && -z "${CASE_ID}" ]]; then
  echo "[ERROR] --pattern requires --case" >&2
  exit 2
fi

if [[ "${FETCH}" -eq 1 ]]; then
  make -C "${ROOT}" fetch-table6-data
fi
args=(reproduce-table6 "THREADS=${THREADS}")
if [[ -n "${CASE_ID}" ]]; then args+=("CASE=${CASE_ID}"); fi
if [[ -n "${PATTERN}" ]]; then args+=("PATTERN=${PATTERN}"); fi
if [[ -n "${ROLE}" ]]; then args+=("ROLE=${ROLE}"); fi
exec make -C "${ROOT}" "${args[@]}"
