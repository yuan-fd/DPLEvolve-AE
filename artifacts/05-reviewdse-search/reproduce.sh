#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODE=plan
CASE_ID=aes_nangate45
RUN_PREFIX=""
ACK=no
THREADS="${THREADS:-10}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --plan) MODE=plan; shift ;;
    --small) MODE=small; shift ;;
    --level1) MODE=level1; shift ;;
    --paper) MODE=paper; shift ;;
    --case) CASE_ID="$2"; shift 2 ;;
    --run-prefix) RUN_PREFIX="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --acknowledge-cost) ACK=yes; shift ;;
    --help|-h)
      sed -n '1,80p' "${ROOT}/artifacts/05-reviewdse-search/README.md"
      exit 0
      ;;
    *) echo "[ERROR] unknown argument: $1" >&2; exit 2 ;;
  esac
done
case "${MODE}" in
  plan) exec make -C "${ROOT}" plan-level1 plan-dse-paper "THREADS=${THREADS}" ;;
  small) exec make -C "${ROOT}" run-dse-small "CASE=${CASE_ID}" "THREADS=${THREADS}" ;;
  level1)
    [[ "${ACK}" == yes ]] || { echo "[ERROR] pass --acknowledge-cost" >&2; exit 2; }
    exec make -C "${ROOT}" reproduce-level1 ACKNOWLEDGE_LLM_COST=yes "THREADS=${THREADS}"
    ;;
  paper)
    [[ "${ACK}" == yes ]] || { echo "[ERROR] pass --acknowledge-cost" >&2; exit 2; }
    [[ -n "${RUN_PREFIX}" ]] || { echo "[ERROR] --paper requires --run-prefix NAME" >&2; exit 2; }
    exec make -C "${ROOT}" run-dse-paper ACKNOWLEDGE_LLM_COST=yes \
      "DSE_RUN_PREFIX=${RUN_PREFIX}" "THREADS=${THREADS}"
    ;;
esac
