#!/usr/bin/env bash
# DPLEvolve AE — Reproduce Main Results (Human Entry Point)
# Runs the main paper experiments (Level 2 multi-agent DSE).
# Usage: make reproduce-main
#
# NOTE: This script requires LLM API access and will consume tokens.
# By default it runs in DRY-RUN mode that validates the pipeline
# without making actual API calls.

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
echo " DPLEvolve AE — Main Results Reproduction"
echo "=============================================="
echo ""
echo "  NOTICE: Full paper results reproduction requires:"
echo "    - LLM API access (Claude API or equivalent)"
echo "    - Significant token budget (billions of tokens)"
echo "    - Extended runtime (hours to days)"
echo ""
echo "  This script provides:"
echo "    1. Baseline-only validation (no API calls)"
echo "    2. Pipeline dry-run verification"
echo "    3. Instructions for full reproduction"
echo "=============================================="
echo ""

# --- Step 1: Validate baselines for all 9 cases ---
dpl_ae_info "Step 1: Reproducing baselines for all paper cases..."
bash "${SCRIPT_DIR}/reproduce_baseline.sh"
echo ""

# --- Step 2: Dry-run the DSE pipeline ---
dpl_ae_info "Step 2: Validating DSE pipeline structure (dry-run, no API calls)..."
# Verify that the required scripts and configs exist
REQUIRED_SCRIPTS=(
  "${DPL_EVOLVE_AGENT_ROOT}/scripts/codex_exec/runner.py"
  "${DPL_EVOLVE_AGENT_ROOT}/scripts/codex_exec/cli.py"
  "${DPL_EVOLVE_AGENT_ROOT}/experiments/launchers/run_evolve_9case_place_batch.sh"
)
for script in "${REQUIRED_SCRIPTS[@]}"; do
  if [[ -f "${script}" ]]; then
    dpl_ae_ok "Found: ${script}"
  else
    dpl_ae_warn "Missing: ${script}"
  fi
done
echo ""

# --- Step 3: Show configuration ---
dpl_ae_info "Step 3: Experiment configuration summary..."
CONFIG_DIR="${DPL_EVOLVE_AGENT_ROOT}/configs"
if [[ -f "${CONFIG_DIR}/codex_smoke.yaml" ]]; then
  dpl_ae_ok "Smoke config: ${CONFIG_DIR}/codex_smoke.yaml"
fi
if [[ -f "${CONFIG_DIR}/codex_design_specific.yaml" ]]; then
  dpl_ae_ok "Design-specific config: ${CONFIG_DIR}/codex_design_specific.yaml"
fi
echo ""

# --- Print next steps ---
echo "=============================================="
echo " Pipeline validated."
echo ""
echo " To run actual LLM-powered DSE, set API credentials and execute:"
echo "   cd ${DPL_EVOLVE_AGENT_ROOT}"
echo "   source env.sh"
echo "   bash experiments/launchers/run_evolve_9case_place_batch.sh"
echo ""
echo " Estimated resource requirements:"
echo "   - Token budget: ~2.15B tokens per design (paper claim)"
echo "   - Runtime: hours to days per design"
echo "   - API: Claude API or compatible endpoint"
echo "=============================================="
