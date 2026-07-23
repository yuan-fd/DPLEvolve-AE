#!/usr/bin/env bash
# Fresh Table 5 execution. Currently exits BLOCKED when recovery data is absent.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
THREADS="${THREADS:-10}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --threads) THREADS="${2:?--threads requires a value}"; shift 2 ;;
    --help|-h)
      echo "Usage: artifacts/02-table5-composability/reproduce.sh [--threads N]"
      echo "Requires the recovered SWERV DENSE_2 config and all six source trees."
      exit 0
      ;;
    *) echo "[ERROR] unknown argument: $1" >&2; exit 2 ;;
  esac
done
exec bash "${ROOT}/scripts/reproduce/reproduce_table5.sh" --threads "${THREADS}"
