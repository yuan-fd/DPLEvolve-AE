#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

THREADS="${THREADS:-10}"
MAX_PARALLEL="${MAX_PARALLEL:-1}"
SWERV_DESIGN_CONFIG="${TABLE5_SWERV_DESIGN_CONFIG:-${PAPER_DATA_ROOT}/table5/swerv_dense_n45/input/config_dense2.mk}"

usage() {
  cat <<'EOF'
Usage: prepare_table5_inputs.sh [options]

Regenerate the dense Nangate45 inputs used by Table 5. AES DENSE and JPEG
DENSE have retained recipes. SWERV DENSE_2 requires its deleted, untracked
config_dense2.mk; the standard SWERV config is not used as a silent substitute.

Options:
  --threads N                  ORFS threads. Default: 10.
  --max-parallel N             Per-batch parallel jobs. Default: 1.
  --swerv-design-config FILE   Recovered exact config_dense2.mk.
  --dry-run                    Print commands without executing them.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --threads) THREADS="$2"; shift 2 ;;
    --max-parallel) MAX_PARALLEL="$2"; shift 2 ;;
    --swerv-design-config) SWERV_DESIGN_CONFIG="$(realpath -m "$2")"; shift 2 ;;
    --dry-run) REPRO_DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) repro_die "unknown argument: $1" ;;
  esac
done
repro_positive_integer threads "${THREADS}"
repro_positive_integer max-parallel "${MAX_PARALLEL}"
if [[ ! -f "${SWERV_DESIGN_CONFIG}" ]]; then
  echo "[BLOCKED] Exact SWERV DENSE_2 config is missing: ${SWERV_DESIGN_CONFIG}" >&2
  echo "          The paper-time untracked config_dense2.mk was deleted; standard config.mk is not equivalent." >&2
  exit 3
fi
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
  if [[ "${core_utilization}" == default ]]; then
    repro_run "${command[@]}"
  else
    repro_run env CORE_UTILIZATION="${core_utilization}" "${command[@]}"
  fi
}

# These are the retained paper recipes. JPEG's config uses
# CORE_UTILIZATION ?=, so the 90 override is required.
run_input aes_dense_nangate45 DENSE default
run_input jpeg_util90_nangate45 DENSE 90

# The paper-time DENSE_2 flow was launched with an untracked config_dense2.mk.
# Invoke that recovered file directly rather than the case registry's standard
# swerv_wrapper/config.mk.
repro_note "Table 5 input: case=swerv_wrapper_nangate45 FLOW_VARIANT=DENSE_2 DESIGN_CONFIG=${SWERV_DESIGN_CONFIG}"
repro_run make -C "${ORFS_ROOT}/flow" --no-print-directory \
  DESIGN_CONFIG="${SWERV_DESIGN_CONFIG}" FLOW_VARIANT=DENSE_2 \
  NUM_CORES="${THREADS}" check-openroad check-yosys place

repro_note "Table 5 dense inputs regenerated under ORFS flow/results"
