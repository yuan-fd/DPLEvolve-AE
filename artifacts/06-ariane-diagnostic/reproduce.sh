#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
THREADS="${THREADS:-10}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --threads) THREADS="${2:?--threads requires a value}"; shift 2 ;;
    --help|-h)
      sed -n '1,80p' "${ROOT}/artifacts/06-ariane-diagnostic/README.md"
      exit 0
      ;;
    *) echo "[ERROR] unknown argument: $1" >&2; exit 2 ;;
  esac
done
exec make -C "${ROOT}" reproduce-ariane-diagnostic "THREADS=${THREADS}"
