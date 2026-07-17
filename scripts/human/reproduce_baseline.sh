#!/usr/bin/env bash
# DPLEvolve AE — Reproduce Baseline Results (Human Entry Point)
# Runs the three canonical baseline lines on all paper cases.
# Usage: make reproduce-baseline

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export AE_ROOT

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/lib/env_vars.sh"
dpl_ae_resolve_env

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/lib/utils.sh"

echo "=============================================="
echo " DPLEvolve AE — Baseline Reproduction"
echo "=============================================="
echo ""

CASES=("aes_nangate45" "ibex_nangate45" "jpeg_nangate45" "ariane133_nangate45" "bp_quad_nangate45")
STAMP="$(dpl_ae_timestamp)"
THREADS="${THREADS:-8}"

dpl_ae_info "Cases: ${CASES[*]}"
dpl_ae_info "Threads: ${THREADS}"
dpl_ae_info "Timestamp: ${STAMP}"
echo ""

total=${#CASES[@]}
completed=0

for case in "${CASES[@]}"; do
  completed=$((completed + 1))
  echo "--- [${completed}/${total}] ${case} ---"

  DESIGN_CONFIG="designs/nangate45/${case#*_}/config.mk"
  FLOW_VARIANT="ae_baseline_${case}_${STAMP}"

  # Run the 3-line baseline suite
  bash "${DPL_EVOLVE_AGENT_ROOT}/baseline/run_baseline_suite.sh" \
    --case "${case}" \
    --flow-variant "${FLOW_VARIANT}" \
    --threads "${THREADS}" \
    --tag-prefix "ae_baseline"

  dpl_ae_ok "${case} baseline complete"
  echo ""
done

echo ""
dpl_ae_ok "=============================================="
dpl_ae_ok " All baselines complete (${total} cases)"
dpl_ae_ok " Results: ${ORFS_ROOT}/flow/reports/"
dpl_ae_ok "=============================================="
