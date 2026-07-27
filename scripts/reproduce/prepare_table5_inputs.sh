#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

THREADS="${THREADS:-10}"
MAX_PARALLEL="${MAX_PARALLEL:-1}"

usage() {
  cat <<'EOF'
Usage: prepare_table5_inputs.sh [options]

Regenerate the three dense Nangate45 inputs used only by Table 5. The recorded
70/90/60 utilization overrides are passed to the pinned ORFS flow without
editing its tracked design configurations.

Options:
  --threads N                  ORFS threads. Default: 10.
  --max-parallel N             Per-batch parallel jobs. Default: 1.
  --dry-run                    Print commands without executing them.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --threads) THREADS="$2"; shift 2 ;;
    --max-parallel) MAX_PARALLEL="$2"; shift 2 ;;
    --dry-run) REPRO_DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) repro_die "unknown argument: $1" ;;
  esac
done
repro_positive_integer threads "${THREADS}"
repro_positive_integer max-parallel "${MAX_PARALLEL}"
if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then repro_require_runtime; fi

run_input() {
  local case_id="$1" flow_variant="$2" core_utilization="$3"
  repro_note "Table 5 input: case=${case_id} FLOW_VARIANT=${flow_variant} CORE_UTILIZATION=${core_utilization}"
  local command=("${DPL_EVOLVE_AGENT_ROOT}/scripts/evaluator/run_place_batch.sh"
      --case "${case_id}" \
      --flow-variant "${flow_variant}" \
      --target place \
      --max-tasks "${MAX_PARALLEL}" \
      --num-cores "${THREADS}")
  repro_run env CORE_UTILIZATION="${core_utilization}" "${command[@]}"
}

# These overrides are local to Table 5 input preparation. They do not modify
# the case registry, the tracked ORFS configurations, or any Table 4 profile.
run_input aes_dense_nangate45 DENSE 70
run_input jpeg_util90_nangate45 DENSE 90
run_input swerv_wrapper_nangate45 DENSE_2 60

repro_note "Table 5 dense inputs regenerated under ORFS flow/results"
