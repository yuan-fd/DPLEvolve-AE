#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SOURCE=retained
RUN_PREFIX=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --fresh) SOURCE=fresh; shift ;;
    --run-prefix) RUN_PREFIX="$2"; shift 2 ;;
    --help|-h)
      echo "Usage: artifacts/04-figures/reproduce.sh [--fresh --run-prefix NAME]"
      exit 0
      ;;
    *) echo "[ERROR] unknown argument: $1" >&2; exit 2 ;;
  esac
done
if [[ "${SOURCE}" == fresh && -z "${RUN_PREFIX}" ]]; then
  echo "[ERROR] --fresh requires --run-prefix NAME" >&2
  exit 2
fi
exec make -C "${ROOT}" reproduce-figures "FIGURE_SOURCE=${SOURCE}" "DSE_RUN_PREFIX=${RUN_PREFIX}"
