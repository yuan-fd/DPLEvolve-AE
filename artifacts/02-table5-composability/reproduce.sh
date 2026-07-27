#!/usr/bin/env bash
# Fresh Table 5 execution from three retained program snapshots.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
THREADS="${THREADS:-10}"
DRY_RUN=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --threads) THREADS="${2:?--threads requires a value}"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    --help|-h)
      echo "Usage: artifacts/02-table5-composability/reproduce.sh [--threads N] [--dry-run]"
      echo "Uses the retained LEGALM, Diamond, and Negotiation source snapshots."
      exit 0
      ;;
    *) echo "[ERROR] unknown argument: $1" >&2; exit 2 ;;
  esac
done
args=(--threads "${THREADS}")
if [[ "${DRY_RUN}" -eq 1 ]]; then args+=(--dry-run); fi
exec bash "${ROOT}/scripts/reproduce/reproduce_table5.sh" "${args[@]}"
