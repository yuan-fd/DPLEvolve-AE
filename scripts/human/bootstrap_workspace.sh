#!/usr/bin/env bash
# Create the pinned, patched ORFS workspace without sudo.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
LOCK="${AE_ROOT}/provenance/source-commits.json"
AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT:-${AE_ROOT}/src/dpl_evolve_agent}"
ORFS_ROOT="${ORFS_ROOT:-$(realpath -m "${AE_ROOT}/../OpenROAD-flow-scripts")}"
STATE_ROOT="${DPL_EVOLVE_STATE_ROOT:-$(realpath -m "${AE_ROOT}/../dpl_evolve_state")}"

for command in git python3 rsync; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    echo "[ERROR] missing prerequisite: ${command}" >&2
    exit 1
  fi
done

read_lock() {
  python3 -c 'import json,sys; value=json.load(open(sys.argv[1])); [value := value[key] for key in sys.argv[2].split(".")]; print(value)' "${LOCK}" "$1"
}

orfs_url="$(read_lock repositories.openroad_flow_scripts.url)"
orfs_base="$(read_lock repositories.openroad_flow_scripts.base_commit)"
orfs_tree="$(read_lock repositories.openroad_flow_scripts.prepared_tree)"
openroad_tree="$(read_lock repositories.openroad.prepared_tree)"

if [[ ! -d "${ORFS_ROOT}/.git" ]]; then
  if [[ -e "${ORFS_ROOT}" && -n "$(find "${ORFS_ROOT}" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]]; then
    echo "[ERROR] target exists and is not an ORFS git checkout: ${ORFS_ROOT}" >&2
    exit 1
  fi
  echo "[INFO] cloning ORFS into ${ORFS_ROOT}"
  git clone --filter=blob:none --no-checkout "${orfs_url}" "${ORFS_ROOT}"
  git -C "${ORFS_ROOT}" checkout --detach "${orfs_base}"
else
  echo "[INFO] reusing existing ORFS checkout: ${ORFS_ROOT}"
fi

if [[ -d "${ORFS_ROOT}/tools/OpenROAD" ]]; then
  current_orfs_tree="$(git -C "${ORFS_ROOT}" rev-parse 'HEAD^{tree}')"
  current_openroad_tree="$(git -C "${ORFS_ROOT}/tools/OpenROAD" rev-parse 'HEAD^{tree}')"
  if [[ "${current_orfs_tree}" == "${orfs_tree}" && "${current_openroad_tree}" == "${openroad_tree}" ]]; then
    echo "[PASS] existing ORFS/OpenROAD workspace matches both recorded source trees"
    exit 0
  fi
fi

export DPL_EVOLVE_AGENT_ROOT="${AGENT_ROOT}"
export DPL_EVOLVE_STATE_ROOT="${STATE_ROOT}"
export DPL_EVOLVE_PYTHON="${DPL_EVOLVE_PYTHON:-python3}"
export ORFS_ROOT
export GIT_AUTHOR_NAME="DPLEvolve AE Bootstrap"
export GIT_AUTHOR_EMAIL="dplevolve-ae@example.invalid"
export GIT_COMMITTER_NAME="${GIT_AUTHOR_NAME}"
export GIT_COMMITTER_EMAIL="${GIT_AUTHOR_EMAIL}"

bash "${AGENT_ROOT}/scripts/workspace/prepare_workspace.sh" \
  --workspace-root "${ORFS_ROOT}" \
  --seed-root "${STATE_ROOT}/seed_sources" \
  --orfs-branch dplevolve-ae-prepared \
  --openroad-branch dplevolve-ae-prepared

actual_orfs_tree="$(git -C "${ORFS_ROOT}" rev-parse 'HEAD^{tree}')"
actual_openroad_tree="$(git -C "${ORFS_ROOT}/tools/OpenROAD" rev-parse 'HEAD^{tree}')"
if [[ "${actual_orfs_tree}" != "${orfs_tree}" ]]; then
  echo "[ERROR] prepared ORFS tree mismatch: ${actual_orfs_tree}" >&2
  exit 1
fi
if [[ "${actual_openroad_tree}" != "${openroad_tree}" ]]; then
  echo "[ERROR] prepared OpenROAD tree mismatch: ${actual_openroad_tree}" >&2
  exit 1
fi

echo "[PASS] clean ORFS/OpenROAD workspace matches both recorded source trees"
echo "Next: make setup"
