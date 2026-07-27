#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

TRACK="hpwl"
THREADS="${THREADS:-8}"
FLOW_VARIANT="${FLOW_VARIANT:-paper9_place}"
CASES=()
SOURCE_RUNNER="${AE_ROOT}/artifacts/01-table4-qor/selected-programs/run.sh"

usage() {
  cat <<'EOF'
Usage: replay_selected.sh --track hpwl|ghr [--case ID ...] [--flow-variant NAME] [--threads N] [--dry-run]

Build each frozen Table 4 ReviewDSE source tree and run the protected complete
detailed-placement trajectory on its own target ODB. The artifact still
requires configured Teacher and Student Agents; this fixed replay normally
issues no new model request and is not an alternative ReviewDSE path.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --track) TRACK="$2"; shift 2 ;;
    --case) CASES+=("$2"); shift 2 ;;
    --flow-variant) FLOW_VARIANT="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --dry-run) REPRO_DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) repro_die "unknown argument: $1" ;;
  esac
done

[[ "${TRACK}" == hpwl || "${TRACK}" == ghr ]] || repro_die "--track must be hpwl or ghr"
repro_positive_integer threads "${THREADS}"
if [[ "${#CASES[@]}" -eq 0 ]]; then
  CASES=(
    aes_asap7 aes_nangate45 ariane133_nangate45 ibex_asap7
    ibex_nangate45 jpeg_asap7 jpeg_nangate45
    swerv_wrapper_asap7 swerv_wrapper_nangate45
  )
fi
if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then repro_require_runtime; fi

for case_id in "${CASES[@]}"; do
  run_id="paper_table4_${TRACK}_${case_id}"
  args=(--case "${case_id}" --objective "${TRACK}" --flow-variant "${FLOW_VARIANT}" --threads "${THREADS}" --output-root "${REPRO_OUTPUT_ROOT}/table4" --run-id "${run_id}")
  if [[ "${REPRO_DRY_RUN}" -eq 1 ]]; then args+=(--dry-run); fi
  repro_run "${SOURCE_RUNNER}" "${args[@]}"
done
