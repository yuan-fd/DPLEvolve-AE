#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${ROOT}/../../.." && pwd)"
PYTHON_BIN="${DPL_EVOLVE_PYTHON:-python3}"
AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT:-${AE_ROOT}/src/dpl_evolve_agent}"
ORFS_ROOT="${ORFS_ROOT:-${AE_ROOT}/../OpenROAD-flow-scripts}"
STATE_ROOT="${DPL_EVOLVE_STATE_ROOT:-${AE_ROOT}/../dpl_evolve_state}"
THREADS=8
CASE_ID=""
OBJECTIVE="hpwl"
FLOW_VARIANT=""
OUTPUT_ROOT=""
RUN_ID=""
DRY_RUN=0
REQUIRE_INPUTS=0
SOURCES_ONLY=0

usage() {
  echo "Usage: $0 [--check] [--sources-only] [--require-inputs] [--case CASE] [--objective hpwl|ghr] [--flow-variant NAME] [--output-root PATH] [--run-id NAME] [--threads N] [--dry-run]"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --check) shift ;;
    --sources-only) SOURCES_ONLY=1; shift ;;
    --require-inputs) REQUIRE_INPUTS=1; shift ;;
    --case) CASE_ID="$2"; shift 2 ;;
    --objective) OBJECTIVE="$2"; shift 2 ;;
    --flow-variant) FLOW_VARIANT="$2"; shift 2 ;;
    --output-root) OUTPUT_ROOT="$2"; shift 2 ;;
    --run-id) RUN_ID="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "[ERROR] unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [[ "${OBJECTIVE}" != "hpwl" && "${OBJECTIVE}" != "ghr" ]]; then
  echo "[ERROR] --objective must be hpwl or ghr" >&2
  exit 2
fi

if [[ -z "${FLOW_VARIANT}" ]]; then
  FLOW_VARIANT="$(${PYTHON_BIN} -c 'import json,sys; print(json.load(open(sys.argv[1]))["flow_variant"])' "${ROOT}/manifest.json")"
fi

verify_args=(--root "${ROOT}")
if [[ "${SOURCES_ONLY}" -ne 1 ]]; then
  verify_args+=(--orfs-root "${ORFS_ROOT}")
fi
if [[ -n "${CASE_ID}" ]]; then
  verify_args+=(--case "${CASE_ID}" --objective "${OBJECTIVE}" --flow-variant "${FLOW_VARIANT}")
fi
if [[ "${REQUIRE_INPUTS}" -eq 1 || -n "${CASE_ID}" && "${DRY_RUN}" -eq 0 ]]; then
  verify_args+=(--require-inputs)
fi
"${PYTHON_BIN}" "${ROOT}/verify.py" "${verify_args[@]}"

if [[ -z "${CASE_ID}" ]]; then
  if [[ "${SOURCES_ONLY}" -eq 1 ]]; then
    echo "Source archive integrity check complete."
  else
    echo "Source archive check complete. Use --case CASE after the exact ODB inputs are installed."
  fi
  exit 0
fi

run_stamp="$(date +%Y%m%d_%H%M%S)"
run_id="${RUN_ID:-replay_${OBJECTIVE}_${CASE_ID}_${run_stamp}}"
OUTPUT_ROOT="${OUTPUT_ROOT:-${STATE_ROOT}/paper_reproduction/selected_program_replay}"
run_root="${OUTPUT_ROOT}/${run_id}"
plan="${run_root}/plan.tsv"
mkdir -p "${run_root}"
printf '%s\n' \
  $'# enabled\tcase\tcore_utilization\tflow_variant\tround_id\tstart_kind\tnotes' \
  $'1\t'"${CASE_ID}"$'\tdefault\t'"${FLOW_VARIANT}"$'\t'"${run_id}"$'\tfrozen_source\tT1 no-LLM replay' \
  > "${plan}"

matrix_args=(
  --matrix-id "${run_id}"
  --output-root "${run_root}"
  --candidate-src "${ROOT}/inputs/programs/${OBJECTIVE}/P_${CASE_ID}/dpl_evolve"
  --candidate-label "${OBJECTIVE}_P_${CASE_ID}"
  --plan "${plan}"
  --threads "${THREADS}"
  --max-parallel 1
  --skip-place
  --skip-baseline
)
if [[ "${DRY_RUN}" -eq 1 ]]; then
  matrix_args+=(--dry-run)
fi

export DPL_EVOLVE_AGENT_ROOT="${AGENT_ROOT}"
export DPL_EVOLVE_STATE_ROOT="${STATE_ROOT}"
export ORFS_ROOT
bash "${AGENT_ROOT}/scripts/matrix/run_candidate_matrix.sh" "${matrix_args[@]}"

results="${run_root}/${run_id}/results.tsv"
if [[ "${DRY_RUN}" -eq 0 ]]; then
  "${PYTHON_BIN}" "${ROOT}/verify.py" \
    --root "${ROOT}" --orfs-root "${ORFS_ROOT}" --case "${CASE_ID}" \
    --objective "${OBJECTIVE}" --flow-variant "${FLOW_VARIANT}" --results "${results}"
else
  echo "Dry run complete; no HPWL result was produced."
fi
