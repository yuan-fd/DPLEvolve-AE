#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
AGENT_ROOT="$(realpath -m "${AGENT_ROOT}")"

if [[ -f "${AGENT_ROOT}/env.sh" ]]; then
  # shellcheck source=/dev/null
  source "${AGENT_ROOT}/env.sh"
fi
STATE_ROOT="${DPL_EVOLVE_STATE_ROOT:-$(realpath -m "${AGENT_ROOT}/../dpl_evolve_state")}"
if [[ -f "${STATE_ROOT}/ae/environment.sh" ]]; then
  # shellcheck source=/dev/null
  source "${STATE_ROOT}/ae/environment.sh"
fi

source "${AGENT_ROOT}/scripts/runtime_env.sh"
dpl_init_runtime "check_environment.sh"

strict_hashes=0
if [[ "${1:-}" == "--strict-hashes" ]]; then
  strict_hashes=1
  shift
fi
if [[ $# -ne 0 ]]; then
  echo "Usage: check_environment.sh [--strict-hashes]" >&2
  exit 2
fi

args=(
  --agent-root "${DPL_EVOLVE_AGENT_ROOT}"
  --orfs-root "${ORFS_ROOT}"
  --state-root "${DPL_EVOLVE_STATE_ROOT}"
  --python "${DPL_EVOLVE_PYTHON}"
)
if [[ -n "${OPENROAD_EXE:-}" ]]; then
  args+=(--openroad-binary "${OPENROAD_EXE}")
fi
if [[ -n "${YOSYS_EXE:-}" ]]; then
  args+=(--yosys-binary "${YOSYS_EXE}")
fi
if [[ "${strict_hashes}" -eq 1 ]]; then
  args+=(--strict-hashes)
fi

exec "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/check_environment.py" "${args[@]}"
