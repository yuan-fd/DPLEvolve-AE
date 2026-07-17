#!/usr/bin/env bash
# DPLEvolve AE — Shared Environment Variable Resolution
# Source this in all AE scripts to get consistent variable resolution.
#
# Usage:
#   source "${AE_ROOT}/scripts/lib/env_vars.sh"
#   dpl_ae_resolve_env

set -euo pipefail

dpl_ae_resolve_env() {
  # Resolve AE_ROOT from this file's location
  if [[ -z "${AE_ROOT:-}" ]]; then
    local lib_dir
    lib_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    AE_ROOT="$(cd "${lib_dir}/../.." && pwd)"
    export AE_ROOT
  fi

  # Resolve agent root
  if [[ -z "${DPL_EVOLVE_AGENT_ROOT:-}" ]]; then
    local candidate
    candidate="$(realpath -m "${AE_ROOT}/../dpl_evolve_agent")"
    if [[ -d "${candidate}" ]]; then
      DPL_EVOLVE_AGENT_ROOT="${candidate}"
    fi
  fi
  export DPL_EVOLVE_AGENT_ROOT

  # Resolve ORFS root
  if [[ -z "${ORFS_ROOT:-}" ]]; then
    local candidate
    candidate="$(realpath -m "${AE_ROOT}/../OpenROAD-flow-scripts")"
    if [[ -d "${candidate}/flow" && -d "${candidate}/tools/OpenROAD" ]]; then
      ORFS_ROOT="${candidate}"
    fi
  fi
  export ORFS_ROOT

  # Resolve state root
  if [[ -z "${DPL_EVOLVE_STATE_ROOT:-}" ]]; then
    local candidate
    candidate="$(realpath -m "${AE_ROOT}/../dpl_evolve_state")"
    if [[ -d "${candidate}" ]]; then
      DPL_EVOLVE_STATE_ROOT="${candidate}"
    else
      DPL_EVOLVE_STATE_ROOT="${candidate}"
      mkdir -p "${DPL_EVOLVE_STATE_ROOT}"
    fi
  fi
  export DPL_EVOLVE_STATE_ROOT

  # Resolve Python
  if [[ -z "${DPL_EVOLVE_PYTHON:-}" ]]; then
    # Try the project venv first
    local venv_python
    venv_python="$(realpath -m "${AE_ROOT}/../.venvs/dplevolve/bin/python")"
    if [[ -x "${venv_python}" ]]; then
      DPL_EVOLVE_PYTHON="${venv_python}"
    elif command -v python3 >/dev/null 2>&1; then
      DPL_EVOLVE_PYTHON="$(command -v python3)"
    elif command -v python >/dev/null 2>&1; then
      DPL_EVOLVE_PYTHON="$(command -v python)"
    fi
  fi
  export DPL_EVOLVE_PYTHON

  # Load machine-local environment if available
  if [[ -f "${DPL_EVOLVE_STATE_ROOT}/ae/environment.sh" ]]; then
    # shellcheck source=/dev/null
    source "${DPL_EVOLVE_STATE_ROOT}/ae/environment.sh"
  fi

  # Resolve Yosys binary
  if [[ -z "${YOSYS_EXE:-}" ]]; then
    local candidate
    candidate="${DPL_EVOLVE_STATE_ROOT}/yosys/8449dd470/bin/yosys"
    if [[ -x "${candidate}" ]]; then
      YOSYS_EXE="${candidate}"
    fi
  fi
  export YOSYS_EXE

  # Resolve OpenROAD binary
  if [[ -z "${OPENROAD_EXE:-}" ]]; then
    local candidate
    candidate="${DPL_EVOLVE_STATE_ROOT}/openroad_core/d5ff63a/install/OpenROAD/bin/openroad"
    if [[ -x "${candidate}" ]]; then
      OPENROAD_EXE="${candidate}"
    fi
  fi
  export OPENROAD_EXE

  # Verify critical paths
  local errors=0
  if [[ -z "${DPL_EVOLVE_AGENT_ROOT:-}" || ! -d "${DPL_EVOLVE_AGENT_ROOT}" ]]; then
    echo "[ERROR] DPL_EVOLVE_AGENT_ROOT not found: ${DPL_EVOLVE_AGENT_ROOT:-unset}" >&2
    errors=1
  fi
  if [[ -z "${ORFS_ROOT:-}" || ! -d "${ORFS_ROOT}/flow" ]]; then
    echo "[ERROR] ORFS_ROOT not found or missing flow/: ${ORFS_ROOT:-unset}" >&2
    errors=1
  fi
  if [[ -z "${DPL_EVOLVE_PYTHON:-}" || ! -x "${DPL_EVOLVE_PYTHON}" ]]; then
    echo "[ERROR] Python not found: ${DPL_EVOLVE_PYTHON:-unset}" >&2
    errors=1
  fi
  if [[ "${errors}" -ne 0 ]]; then
    echo "[ERROR] Environment resolution failed. Run 'make setup' first." >&2
    return 1
  fi

  return 0
}
