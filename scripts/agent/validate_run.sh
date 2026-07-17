#!/usr/bin/env bash
# DPLEvolve AE — Agent Run Validation
# Validates experiment output against expected results.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export AE_ROOT

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/lib/env_vars.sh"
dpl_ae_resolve_env

# shellcheck source=/dev/null
source "${AE_ROOT}/scripts/lib/utils.sh"

EXPERIMENT=""
CHECK_ALL=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --experiment) EXPERIMENT="$2"; shift 2 ;;
    --check-all) CHECK_ALL=1; shift ;;
    *) shift ;;
  esac
done

echo "=============================================="
echo " DPLEvolve AE — Run Validation"
echo "=============================================="
echo ""

failures=0

# --- Check AES smoke test ---
check_smoke() {
  dpl_ae_info "Validating AES smoke..."
  LOCK="${DPL_EVOLVE_AGENT_ROOT}/metadata/ae_reproduction_lock.json"
  if [[ ! -f "${LOCK}" ]]; then
    dpl_ae_error "Reproduction lock not found: ${LOCK}"
    return 1
  fi

  # Find the reference flow variant from the lock file
  FLOW_HOME="${ORFS_ROOT}/flow"
  local ref_variant
  ref_variant="$(dpl_ae_json_get "${LOCK}" "aes_nangate45_smoke.reference_flow_variant")"
  local ref_tag
  ref_tag="$(dpl_ae_json_get "${LOCK}" "aes_nangate45_smoke.reference_run_tag")"

  local latest_metrics
  if [[ -n "${ref_variant}" && -n "${ref_tag}" ]]; then
    latest_metrics="${FLOW_HOME}/reports/nangate45/aes/${ref_variant}/dpl_evolve_baseline/${ref_tag}/metrics.json"
  fi

  if [[ ! -f "${latest_metrics}" ]]; then
    # Fallback: find any valid AES baseline metrics
    latest_metrics=$(find "${FLOW_HOME}/reports/nangate45/aes/" -name "metrics.json" -path "*/dpl_evolve_baseline/*" 2>/dev/null | sort -r | head -1)
  fi

  if [[ -z "${latest_metrics}" ]]; then
    dpl_ae_error "No smoke metrics.json found."
    dpl_ae_error "Run 'make smoke' first."
    return 1
  fi

  dpl_ae_info "Found: ${latest_metrics}"

  # Read expected values
  local expected_final
  expected_final="$(dpl_ae_json_get "${LOCK}" "aes_nangate45_smoke.expected.final_hpwl_micron")"
  local tol
  tol="$(dpl_ae_json_get "${LOCK}" "aes_nangate45_smoke.absolute_tolerances.final_hpwl_micron")"

  # Read actual value
  local actual_final
  actual_final="$("${DPL_EVOLVE_PYTHON}" -c "
import json, sys
data = json.load(open(sys.argv[1]))
print(data['hpwl_stages']['final_micron'])
" "${latest_metrics}")"

  # Compare
  dpl_ae_float_within_tolerance "${actual_final}" "${expected_final}" "${tol}" "final_hpwl_micron"
  return $?
}

# --- Check all documentation files exist ---
check_docs() {
  dpl_ae_info "Checking documentation completeness..."
  local required=(
    "README.md"
    "LICENSE"
    "CITATION.cff"
    "SECURITY.md"
    "CHANGELOG.md"
    "docs/artifact-overview.md"
    "docs/quickstart.md"
    "docs/environment.md"
    "docs/experiments.md"
    "docs/expected-results.md"
    "docs/troubleshooting.md"
    "docs/claims-to-artifacts.md"
    "docs/artifact-appendix.md"
    "agent/AGENTS.md"
    "agent/context/project-map.md"
    "agent/context/experiment-semantics.md"
    "agent/context/invariants.md"
  )
  local missing=0
  for f in "${required[@]}"; do
    if [[ -f "${AE_ROOT}/${f}" ]]; then
      dpl_ae_ok "${f}"
    else
      dpl_ae_error "MISSING: ${f}"
      missing=1
    fi
  done
  return ${missing}
}

# --- Check no secrets in tracked files ---
check_secrets() {
  dpl_ae_info "Checking for secrets in tracked files..."
  # Patterns that indicate REAL secrets (not documentation examples)
  # sk-ant-api-... with 40+ chars after = actual key
  # We search for patterns where the value is NOT "...", "your-key-here", or empty
  local found=0

  # Check for real-looking API keys (40+ char base64-like strings)
  local api_key_matches
  api_key_matches="$(grep -rP 'sk-ant-api-[A-Za-z0-9+/=]{40,}' \
    "${AE_ROOT}/docs/" "${AE_ROOT}/agent/" "${AE_ROOT}/scripts/" \
    --include="*.sh" --include="*.md" --include="*.py" 2>/dev/null || true)"
  if [[ -n "${api_key_matches}" ]]; then
    dpl_ae_warn "Found potential REAL API key:"
    echo "${api_key_matches}"
    found=1
  fi

  # Check for env files that might have been accidentally committed
  local env_matches
  env_matches="$(find "${AE_ROOT}" -name ".env" -o -name "*.key" -o -name "*.token" 2>/dev/null || true)"
  if [[ -n "${env_matches}" ]]; then
    dpl_ae_warn "Found potential credential files:"
    echo "${env_matches}"
    found=1
  fi

  if [[ "${found}" -eq 0 ]]; then
    dpl_ae_ok "No secrets found in tracked files"
  fi
  return ${found}
}

# --- Main ---
if [[ "${CHECK_ALL}" -eq 1 || "${EXPERIMENT}" == "smoke" ]]; then
  check_smoke || failures=$((failures + 1))
fi

if [[ "${CHECK_ALL}" -eq 1 ]]; then
  check_docs || failures=$((failures + 1))
  check_secrets || failures=$((failures + 1))
fi

echo ""
if [[ "${failures}" -eq 0 ]]; then
  dpl_ae_ok "=============================================="
  dpl_ae_ok " All validations passed"
  dpl_ae_ok "=============================================="
  exit 0
else
  dpl_ae_error "=============================================="
  dpl_ae_error " ${failures} validation(s) failed"
  dpl_ae_error "=============================================="
  exit 1
fi
