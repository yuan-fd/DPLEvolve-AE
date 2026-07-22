#!/usr/bin/env bash
set -euo pipefail

REPRO_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="${AE_ROOT:-$(cd "${REPRO_SCRIPT_DIR}/../.." && pwd)}"
DPL_EVOLVE_AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT:-${AE_ROOT}/src/dpl_evolve_agent}"
ORFS_ROOT="${ORFS_ROOT:-$(realpath -m "${AE_ROOT}/../OpenROAD-flow-scripts")}"
DPL_EVOLVE_STATE_ROOT="${DPL_EVOLVE_STATE_ROOT:-$(realpath -m "${AE_ROOT}/../dpl_evolve_state")}"
DPL_EVOLVE_PYTHON="${DPL_EVOLVE_PYTHON:-python3}"
REPRO_OUTPUT_ROOT="${REPRO_OUTPUT_ROOT:-${DPL_EVOLVE_STATE_ROOT}/paper_reproduction}"
PAPER_DATA_ROOT="${PAPER_DATA_ROOT:-${AE_ROOT}/paper-data}"
REPRO_DRY_RUN="${REPRO_DRY_RUN:-0}"

export AE_ROOT DPL_EVOLVE_AGENT_ROOT ORFS_ROOT DPL_EVOLVE_STATE_ROOT
export DPL_EVOLVE_PYTHON REPRO_OUTPUT_ROOT PAPER_DATA_ROOT

repro_die() {
  printf '[ERROR] %s\n' "$*" >&2
  exit 2
}

repro_note() {
  printf '[reproduce] %s\n' "$*"
}

repro_shell_join() {
  local arg
  printf '+'
  for arg in "$@"; do
    printf ' %q' "${arg}"
  done
  printf '\n'
}

repro_run() {
  repro_shell_join "$@"
  if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
    "$@"
  fi
}

repro_require_runtime() {
  [[ -d "${DPL_EVOLVE_AGENT_ROOT}" ]] || repro_die "framework missing: ${DPL_EVOLVE_AGENT_ROOT}"
  [[ -d "${ORFS_ROOT}/flow" ]] || repro_die "ORFS workspace missing: ${ORFS_ROOT}; run 'make bootstrap'"
  [[ -x "${DPL_EVOLVE_PYTHON}" ]] || command -v "${DPL_EVOLVE_PYTHON}" >/dev/null 2>&1 \
    || repro_die "Python is not executable: ${DPL_EVOLVE_PYTHON}"
}

repro_positive_integer() {
  local name="$1"
  local value="$2"
  [[ "${value}" =~ ^[1-9][0-9]*$ ]] || repro_die "${name} must be a positive integer, got '${value}'"
}
