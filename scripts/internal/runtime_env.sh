#!/usr/bin/env bash
# DPLEvolve AE — Runtime Environment Bootstrap
# Provides the same init logic as dpl_evolve_agent/scripts/runtime_env.sh
# but adapted for the AE repo's directory layout.
#
# Usage:
#   source "${AE_ROOT}/scripts/internal/runtime_env.sh"
#   dpl_ae_init_runtime "script_name"

set -euo pipefail

_dpl_ae_is_orfs_root() {
  local candidate="$1"
  [[ -d "${candidate}/flow" && -d "${candidate}/tools/OpenROAD" ]]
}

dpl_ae_init_runtime() {
  local script_name="${1:-ae_script}"

  # Resolve AE_ROOT
  if [[ -z "${AE_ROOT:-}" ]]; then
    local internal_dir
    internal_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    AE_ROOT="$(cd "${internal_dir}/../.." && pwd)"
    export AE_ROOT
  fi

  # Load shared env vars
  # shellcheck source=/dev/null
  source "${AE_ROOT}/scripts/lib/env_vars.sh"
  dpl_ae_resolve_env

  # Load utils
  # shellcheck source=/dev/null
  source "${AE_ROOT}/scripts/lib/utils.sh"

  # Source the agent's env.sh for machine-local settings
  if [[ -f "${DPL_EVOLVE_AGENT_ROOT}/env.sh" ]]; then
    # shellcheck source=/dev/null
    source "${DPL_EVOLVE_AGENT_ROOT}/env.sh"
  fi

  # Source the agent's runtime_env.sh for dpl_init_runtime and friends
  if [[ -f "${DPL_EVOLVE_AGENT_ROOT}/scripts/runtime_env.sh" ]]; then
    # shellcheck source=/dev/null
    source "${DPL_EVOLVE_AGENT_ROOT}/scripts/runtime_env.sh"
    if declare -f dpl_init_runtime >/dev/null 2>&1; then
      dpl_init_runtime "${script_name}"
      return 0
    fi
  fi

  # Fallback: run the AE-only resolution (simpler, for when agent tree unavailable)
  dpl_ae_info "Using AE-native runtime initialization (agent runtime_env.sh not available)"

  # Verify critical paths
  if [[ -z "${ORFS_ROOT:-}" || ! -d "${ORFS_ROOT}/flow" ]]; then
    dpl_ae_error "ORFS_ROOT not properly resolved: ${ORFS_ROOT:-unset}"
    return 1
  fi
  if [[ -z "${DPL_EVOLVE_PYTHON:-}" || ! -x "${DPL_EVOLVE_PYTHON}" ]]; then
    dpl_ae_error "Python not properly resolved: ${DPL_EVOLVE_PYTHON:-unset}"
    return 1
  fi

  export FLOW_HOME="${ORFS_ROOT}/flow"

  dpl_ae_info "Runtime initialized: ${script_name}"
  return 0
}
