#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

FLOW_VARIANT="paper9_place"
THREADS="${THREADS:-16}"
MAX_PARALLEL="${MAX_PARALLEL:-3}"
CASES=()

usage() {
  cat <<'EOF'
Usage: prepare_paper_inputs.sh [options]

Generate the incoming placement ODBs for the nine Table 4 target tasks using
the pinned ORFS/Yosys/OpenROAD checkout. These ODBs are the inputs to the
detailed-placement experiments; this command does not run BO or ReviewDSE.

Options:
  --case ID             Generate one case; may be repeated. Default: all nine.
  --flow-variant NAME   Output FLOW_VARIANT. Default: paper9_place.
  --threads N           Threads per ORFS job. Default: 16.
  --max-parallel N      Concurrent ORFS jobs. Default: 3.
  --dry-run             Print every real ORFS command without executing it.
  --help                Show this message.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --case) CASES+=("$2"); shift 2 ;;
    --flow-variant) FLOW_VARIANT="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --max-parallel) MAX_PARALLEL="$2"; shift 2 ;;
    --dry-run) REPRO_DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) repro_die "unknown argument: $1" ;;
  esac
done

repro_positive_integer threads "${THREADS}"
repro_positive_integer max-parallel "${MAX_PARALLEL}"
if [[ "${#CASES[@]}" -eq 0 ]]; then
  CASES=(
    aes_asap7 aes_nangate45 ariane133_nangate45 ibex_asap7
    ibex_nangate45 jpeg_asap7 jpeg_nangate45
    swerv_wrapper_asap7 swerv_wrapper_nangate45
  )
fi

if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
  repro_require_runtime
fi

repro_note "preparing ${#CASES[@]} Table 4 input snapshots (FLOW_VARIANT=${FLOW_VARIANT})"
for case_id in "${CASES[@]}"; do
  repro_run "${DPL_EVOLVE_AGENT_ROOT}/scripts/evaluator/run_place_batch.sh" \
    --case "${case_id}" \
    --flow-variant "${FLOW_VARIANT}" \
    --target place \
    --max-tasks "${MAX_PARALLEL}" \
    --num-cores "${THREADS}"
done

if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
  record_args=(
    "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/record_table4_inputs.py"
    --orfs-root "${ORFS_ROOT}"
    --flow-variant "${FLOW_VARIANT}"
    --selected-manifest "${AE_ROOT}/artifacts/01-table4-qor/selected-programs/manifest.json"
    --output "${REPRO_OUTPUT_ROOT}/inputs/${FLOW_VARIANT}/manifest.json"
  )
  for case_id in "${CASES[@]}"; do record_args+=(--case "${case_id}"); done
  repro_run "${record_args[@]}"
fi

repro_note "input preparation complete; next run 'make validate-evaluator'"
