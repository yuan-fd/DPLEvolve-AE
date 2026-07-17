#!/usr/bin/env bash
# DPLEvolve AE — Generate Tables from Structured Results
# Usage: make table-1   OR   ./scripts/human/generate_tables.sh --table 1

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export AE_ROOT

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/lib/env_vars.sh"
dpl_ae_resolve_env

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/lib/utils.sh"

TABLE=""
FIGURE=""
RESULTS_DIR="${AE_ROOT}/results"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --table) TABLE="$2"; shift 2 ;;
    --figure) FIGURE="$2"; shift 2 ;;
    *) shift ;;
  esac
done

generate_table_1() {
  echo "=== Table 1: Baseline Comparison (openroad_dpl_flow vs negotiation vs evolve_default) ==="
  echo ""
  echo "This table compares the three canonical baseline lines on AES/Nangate45."
  echo ""

  SUITE_TSV="${ORFS_ROOT}/flow/reports/nangate45/aes/ae_probe_aes_n45_yosys8449_20260717/dpl_evolve_baseline/suite_runs.tsv"

  if [[ -f "${SUITE_TSV}" ]]; then
    echo "Reference baseline data:"
    echo "----------------------------------------"
    column -t -s $'\t' "${SUITE_TSV}" 2>/dev/null || cat "${SUITE_TSV}"
    echo "----------------------------------------"
  else
    echo "[WARN] Reference baseline data not found at: ${SUITE_TSV}"
    echo "Run 'make reproduce-baseline' first to generate baseline data."
  fi

  echo ""
  echo "Output: ${RESULTS_DIR}/tables/table_1_baseline_comparison.csv"
  # Generate CSV if we have data
  if [[ -f "${SUITE_TSV}" ]]; then
    mkdir -p "${RESULTS_DIR}/tables"
    cp "${SUITE_TSV}" "${RESULTS_DIR}/tables/table_1_baseline_comparison.tsv"
    dpl_ae_ok "Table 1 generated"
  fi
}

generate_table_2() {
  echo "=== Table 2: Main HPWL Results ==="
  echo ""
  echo "This table shows the main paper results comparing ReviewDSE vs BO baseline."
  echo ""
  echo "[INFO] Table 2 requires the full LLM-powered DSE experiment results."
  echo "[INFO] These are not yet reproduced in the current environment."
  echo "[INFO] Reference values from the paper:"
  echo "  - ReviewDSE HPWL improvement: 1.78% average"
  echo "  - Black-box BO improvement:    0.38% average"
  echo ""
  echo "To generate this table with reproduced data, first run: make reproduce-main"
}

generate_table_3() {
  echo "=== Table 3: Constraint Repair Results ==="
  echo ""
  echo "This table shows the 9 complex constraint scenarios repaired by ReviewDSE."
  echo ""
  echo "[INFO] Table 3 requires the full LLM-powered DSE experiment results."
  echo "[INFO] Reference: 9 cases where traditional methods failed/timed out."
  echo ""
  echo "To generate this table with reproduced data, first run: make reproduce-main"
}

generate_figure_3() {
  echo "=== Figure 3: HPWL Improvement Distribution ==="
  echo ""
  echo "[INFO] Figure generation requires matplotlib/seaborn and full experiment results."
  echo "[INFO] Run 'make reproduce-main' first."
}

case "${TABLE:-}${FIGURE:-}" in
  1) generate_table_1 ;;
  2) generate_table_2 ;;
  3) generate_table_3 ;;
  3figure) generate_figure_3 ;;
  *) echo "Usage: generate_tables.sh --table 1|2|3   OR   --figure 3" && exit 1 ;;
esac
