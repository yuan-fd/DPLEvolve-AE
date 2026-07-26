#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOTSTRAP_AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
BOOTSTRAP_AGENT_ROOT="$(realpath -m "${BOOTSTRAP_AGENT_ROOT}")"
source "${BOOTSTRAP_AGENT_ROOT}/scripts/runtime_env.sh"
# shellcheck source=git_prepare_helpers.sh
source "${SCRIPT_DIR}/git_prepare_helpers.sh"
AGENT_ROOT="$(realpath -m "${BOOTSTRAP_AGENT_ROOT}")"
export DPL_EVOLVE_AGENT_ROOT="${AGENT_ROOT}"
dpl_source_local_env_preserving_overrides "${AGENT_ROOT}"
AGENT_ROOT="$(dpl_resolve_agent_root)"
export DPL_EVOLVE_AGENT_ROOT="${AGENT_ROOT}"
DEFAULT_WORKSPACE_ROOT="${ORFS_ROOT:-$(realpath -m "${AGENT_ROOT}/../OpenROAD-flow-scripts")}"
DEFAULT_STATE_ROOT="${DPL_EVOLVE_STATE_ROOT:-${AGENT_ROOT}/.dpl_evolve_state}"
dpl_resolve_python yaml

workspace_root="${DEFAULT_WORKSPACE_ROOT}"
seed_root="${DEFAULT_STATE_ROOT}/seed_sources"
orfs_branch=""
openroad_branch=""
skip_commit=0
force=0

usage() {
  cat <<'EOF'
Usage: prepare_workspace.sh [options]

Bootstrap a clean ORFS/OpenROAD workspace for dpl_evolve experiments.

Actions:
  1. Verify or reset the workspace to the supported ORFS anchor.
  2. Verify or reset OpenROAD to the supported OpenROAD anchor.
  3. Apply the ORFS/OpenROAD prepare patches if needed.
  4. Create local commits for the prepared states unless --skip-commit is used.

Options:
  --workspace-root PATH   ORFS workspace root. Default: parent of this repo.
  --seed-root PATH        Directory for prepared seed source snapshots.
                          Default: $DPL_EVOLVE_STATE_ROOT/seed_sources.
  --orfs-branch NAME      Optional local ORFS branch to create/reset before patching.
  --openroad-branch NAME  Optional local OpenROAD branch to create/reset before patching.
  --skip-commit           Do not create local commits after patching.
  --force                 Reset branches even if they already exist.
  --help                  Show this message.

Environment:
  DPL_EVOLVE_OPENROAD_URL Optional local git-config override for the
                          tools/OpenROAD submodule URL. This is useful for
                          local clean clones whose relative .gitmodules URL
                          would otherwise resolve to a missing sibling repo.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --workspace-root)
      workspace_root="$2"
      shift 2
      ;;
    --seed-root)
      seed_root="$2"
      shift 2
      ;;
    --orfs-branch)
      orfs_branch="$2"
      shift 2
      ;;
    --openroad-branch)
      openroad_branch="$2"
      shift 2
      ;;
    --skip-commit)
      skip_commit=1
      shift
      ;;
    --force)
      force=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[ERROR] Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

workspace_root="$(realpath -m "${workspace_root}")"
seed_root="$(realpath -m "${seed_root}")"
anchors_json="${AGENT_ROOT}/metadata/anchors.json"
orfs_make_patch="${AGENT_ROOT}/patches/orfs_make_overlay.patch"
openroad_patch="${AGENT_ROOT}/patches/openroad_dpl_evolve_base.patch"
openroad_framework_patch="${AGENT_ROOT}/patches/openroad_dpl_evolve_framework.patch"
default_negotiation_patch="${AGENT_ROOT}/patches/openroad_dpl_evolve_default_negotiation_seed.patch"
source_topk_diamond_patch="${AGENT_ROOT}/patches/evolved_legalizers/validated_source_topk_diamond.patch"

if ! git -C "${workspace_root}" rev-parse --git-dir >/dev/null 2>&1; then
  echo "[ERROR] workspace root is not a git repository: ${workspace_root}" >&2
  exit 1
fi

mapfile -t anchor_values < <(
  "${DPL_EVOLVE_PYTHON}" - <<'PY' "${anchors_json}"
import json, sys
data = json.load(open(sys.argv[1], encoding="utf-8"))
print(data["workspace_model"]["orfs_base_commit"])
print(data["workspace_model"]["openroad_base_commit"])
PY
)

orfs_base_commit="${anchor_values[0]}"
openroad_base_commit="${anchor_values[1]}"

file_sha256() {
  "${DPL_EVOLVE_PYTHON}" - "$1" <<'PY'
import hashlib
import pathlib
import sys

print(hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest())
PY
}

ensure_clean_or_reset() {
  local repo_root="$1"
  local target_branch="$2"
  local target_commit="$3"
  local label="$4"

  if [[ "${force}" -ne 1 ]]; then
    if [[ -n "$(git -C "${repo_root}" status --short)" ]]; then
      echo "[ERROR] ${label} repo has local changes: ${repo_root}" >&2
      echo "[ERROR] Re-run with --force only if it is safe to reset the branch." >&2
      exit 1
    fi
  fi

  if [[ -n "${target_branch}" ]]; then
    if git -C "${repo_root}" show-ref --verify --quiet "refs/heads/${target_branch}"; then
      if [[ "${force}" -eq 1 ]]; then
        git -C "${repo_root}" checkout -B "${target_branch}" "${target_commit}"
      elif dpl_prepare_branch_is_resumable "${repo_root}" "${target_branch}" "${target_commit}"; then
        echo "[INFO] ${label}: resuming interrupted preparation on ${target_branch}"
      else
        echo "[ERROR] Branch already exists in ${label}: ${target_branch}" >&2
        echo "[ERROR] Re-run with --force to reset it." >&2
        exit 1
      fi
    else
      git -C "${repo_root}" checkout -B "${target_branch}" "${target_commit}"
    fi
  elif [[ "${force}" -eq 1 ]]; then
    git -C "${repo_root}" reset --hard "${target_commit}"
  elif [[ "$(git -C "${repo_root}" rev-parse HEAD)" != "${target_commit}" ]]; then
    echo "[ERROR] ${label} repo is not at the supported anchor: ${target_commit}" >&2
    echo "[ERROR] Current HEAD: $(git -C "${repo_root}" rev-parse HEAD)" >&2
    echo "[ERROR] Check out the anchor first, pass --force to reset the current branch, or pass --${label,,}-branch NAME." >&2
    exit 1
  fi

  if [[ "${force}" -eq 1 ]]; then
    git -C "${repo_root}" clean -fd
  fi
}

apply_patch_if_needed() {
  local repo_root="$1"
  local patch_file="$2"
  local label="$3"

  if [[ ! -s "${patch_file}" ]]; then
    echo "[INFO] ${label}: patch file is empty, skipping ${patch_file}"
    return
  fi

  if git -C "${repo_root}" apply --whitespace=nowarn --check "${patch_file}" >/dev/null 2>&1; then
    git -C "${repo_root}" apply --whitespace=nowarn "${patch_file}"
    echo "[INFO] ${label}: applied $(basename "${patch_file}")"
    return
  fi

  if git -C "${repo_root}" apply --whitespace=nowarn --reverse --check "${patch_file}" >/dev/null 2>&1; then
    echo "[INFO] ${label}: patch already present, skipping $(basename "${patch_file}")"
    return
  fi

  echo "[ERROR] ${label}: patch does not apply cleanly: ${patch_file}" >&2
  exit 1
}

git_submodule_update() {
  local repo_root="$1"
  shift
  if [[ -n "${DPL_EVOLVE_OPENROAD_URL:-}" \
        && ( "${DPL_EVOLVE_OPENROAD_URL}" == /* \
             || "${DPL_EVOLVE_OPENROAD_URL}" == file://* ) ]]; then
    git -c protocol.file.allow=always -C "${repo_root}" "$@"
  else
    git -C "${repo_root}" "$@"
  fi
}

mirror_openroad_nested_submodule_urls() {
  local target_openroad="$1"
  local source_openroad="${DPL_EVOLVE_OPENROAD_URL:-}"
  if [[ -z "${source_openroad}" ]]; then
    return
  fi
  if [[ "${source_openroad}" == file://* ]]; then
    source_openroad="${source_openroad#file://}"
  fi
  if [[ "${source_openroad}" != /* ]]; then
    return
  fi
  if ! git -C "${source_openroad}" rev-parse --git-dir >/dev/null 2>&1; then
    return
  fi

  local copied=0
  local path_key path local_url
  while read -r key url; do
    [[ -n "${key}" && -n "${url}" ]] || continue
    path_key="${key%.url}.path"
    path="$(
      git -C "${source_openroad}" config --get "${path_key}" 2>/dev/null \
        || git -C "${source_openroad}" config --file .gitmodules --get "${path_key}" 2>/dev/null \
        || true
    )"
    if [[ -n "${path}" && -d "${source_openroad}/${path}" ]]; then
      local_url="${source_openroad}/${path}"
    else
      local_url="${url}"
    fi
    git -C "${target_openroad}" config "${key}" "${local_url}"
    if [[ -f "${target_openroad}/.gitmodules" ]]; then
      git -C "${target_openroad}" config --file .gitmodules "${key}" "${local_url}"
    fi
    copied=1
  done < <(
    git -C "${source_openroad}" config --get-regexp '^submodule\..*\.url' 2>/dev/null \
      || git -C "${source_openroad}" config --file .gitmodules --get-regexp '^submodule\..*\.url' 2>/dev/null \
      || true
  )
  if [[ "${copied}" -eq 1 ]]; then
    echo "[INFO] OpenROAD: mirrored nested submodule URLs from ${source_openroad}"
  fi
}

commit_if_dirty() {
  local repo_root="$1"
  local message="$2"
  shift 2
  if [[ "${skip_commit}" -eq 1 ]]; then
    return
  fi
  if [[ -n "$(git -C "${repo_root}" status --short)" ]]; then
    if [[ $# -gt 0 ]]; then
      git -C "${repo_root}" add -A -- "$@"
    elif [[ "${repo_root}" == "${workspace_root}" ]]; then
      git -C "${repo_root}" add -A -- . \
        ":(exclude)evolve_agent" \
        ":(exclude)evolve_agent/**" \
        ":(exclude)tools/OpenROAD" \
        ":(exclude)tools/OpenROAD/**"
    else
      git -C "${repo_root}" add -A
    fi
    git -C "${repo_root}" commit -m "${message}"
  fi
}

snapshot_dpl_seed() {
  local seed_name="$1"
  local require_framework="$2"
  local source_dir="${workspace_root}/tools/OpenROAD/src/dpl_evolve"
  local target_dir="${seed_root}/${seed_name}"

  if [[ ! -f "${source_dir}/CMakeLists.txt" ]]; then
    echo "[WARN] Cannot snapshot ${seed_name}; missing dpl_evolve source: ${source_dir}" >&2
    return
  fi
  if [[ "${require_framework}" == "1" && ! -f "${source_dir}/src/EvolveLegalizer.cpp" ]]; then
    echo "[WARN] Cannot snapshot ${seed_name}; framework source is not present: ${source_dir}" >&2
    return
  fi
  if [[ "${require_framework}" == "0" && -f "${source_dir}/src/EvolveLegalizer.cpp" ]]; then
    if [[ -d "${target_dir}" ]]; then
      echo "[INFO] ${seed_name}: preserving existing seed snapshot at ${target_dir}"
    else
      echo "[WARN] ${seed_name}: current source already includes framework; cannot derive clean diamond seed without a fresh anchored prepare" >&2
    fi
    return
  fi

  mkdir -p "${seed_root}"
  rm -rf "${target_dir}"
  rsync -a --delete \
    --exclude '__pycache__/' \
    --exclude '*.pyc' \
    --exclude '*.o' \
    --exclude '*.a' \
    --exclude '*.so' \
    --exclude 'CMakeFiles/' \
    "${source_dir}/" "${target_dir}/"
  echo "[INFO] Snapshotted ${seed_name}: ${target_dir}"
}

materialize_default_negotiation_seed() {
  local seed_name="default_negotiation_dpl_evolve"
  local source_dir="${seed_root}/framework_dpl_evolve"
  local target_dir="${seed_root}/${seed_name}"
  local patch_sha

  if [[ ! -f "${default_negotiation_patch}" ]]; then
    echo "[WARN] Cannot materialize ${seed_name}; missing patch: ${default_negotiation_patch}" >&2
    return
  fi
  if [[ ! -f "${source_dir}/CMakeLists.txt" ]]; then
    echo "[WARN] Cannot materialize ${seed_name}; missing framework seed: ${source_dir}" >&2
    return
  fi

  rm -rf "${target_dir}"
  rsync -a --delete \
    --exclude '.git/' \
    --exclude '__pycache__/' \
    --exclude '*.pyc' \
    --exclude '*.o' \
    --exclude '*.a' \
    --exclude '*.so' \
    --exclude 'CMakeFiles/' \
    "${source_dir}/" "${target_dir}/"

  git init "${target_dir}" >/dev/null
  git -C "${target_dir}" config user.email "dpl-evolve-agent@example.invalid"
  git -C "${target_dir}" config user.name "dpl-evolve-agent"
  git -C "${target_dir}" add -A
  git -C "${target_dir}" commit -m "Seed framework dpl_evolve source" >/dev/null

  if git -C "${target_dir}" apply --whitespace=nowarn --check -p1 "${default_negotiation_patch}" >/dev/null 2>&1; then
    git -C "${target_dir}" apply --whitespace=nowarn -p1 "${default_negotiation_patch}"
    git -C "${target_dir}" add -A
    git -C "${target_dir}" commit -m "Apply default negotiation seed patch" >/dev/null
    patch_sha="$(file_sha256 "${default_negotiation_patch}")"
    "${DPL_EVOLVE_PYTHON}" - "${target_dir}/.dpl_evolve_seed_manifest.json" "${seed_name}" "${default_negotiation_patch}" "${patch_sha}" <<'PY'
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
payload = {
    "seed_name": sys.argv[2],
    "patch_path": sys.argv[3],
    "patch_sha256": sys.argv[4],
    "apply_base": "framework_dpl_evolve",
    "status": "materialized",
}
path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
PY
    echo "[INFO] Materialized ${seed_name}: ${target_dir}"
  else
    echo "[ERROR] Cannot apply default negotiation patch to ${target_dir}: ${default_negotiation_patch}" >&2
    rm -rf "${target_dir}"
    exit 1
  fi
}

materialize_source_topk_diamond_seed() {
  local seed_name="source_topk_diamond_dpl_evolve"
  local source_dir="${seed_root}/diamond_dpl_evolve"
  local target_dir="${seed_root}/${seed_name}"
  local patch_sha

  if [[ ! -s "${source_topk_diamond_patch}" ]]; then
    echo "[ERROR] Missing validated sourceTopK patch: ${source_topk_diamond_patch}" >&2
    exit 1
  fi
  if [[ ! -f "${source_dir}/CMakeLists.txt" ]]; then
    echo "[ERROR] Cannot materialize ${seed_name}; missing Diamond seed: ${source_dir}" >&2
    exit 1
  fi

  rm -rf "${target_dir}"
  rsync -a --delete \
    --exclude '.git/' \
    --exclude '__pycache__/' \
    --exclude '*.pyc' \
    --exclude '*.o' \
    --exclude '*.a' \
    --exclude '*.so' \
    --exclude 'CMakeFiles/' \
    "${source_dir}/" "${target_dir}/"

  git init "${target_dir}" >/dev/null
  git -C "${target_dir}" config user.email "dpl-evolve-agent@example.invalid"
  git -C "${target_dir}" config user.name "dpl-evolve-agent"
  git -C "${target_dir}" add -A
  git -C "${target_dir}" commit -m "Seed Diamond dpl_evolve source" >/dev/null

  if ! git -C "${target_dir}" apply --whitespace=nowarn --check "${source_topk_diamond_patch}" >/dev/null 2>&1; then
    echo "[ERROR] Validated sourceTopK patch does not apply to ${source_dir}" >&2
    rm -rf "${target_dir}"
    exit 1
  fi
  git -C "${target_dir}" apply --whitespace=nowarn "${source_topk_diamond_patch}"
  git -C "${target_dir}" add -A
  git -C "${target_dir}" commit -m "Apply validated Diamond sourceTopK mechanism" >/dev/null
  patch_sha="$(file_sha256 "${source_topk_diamond_patch}")"
  "${DPL_EVOLVE_PYTHON}" - "${target_dir}/.dpl_evolve_seed_manifest.json" "${seed_name}" "${source_topk_diamond_patch}" "${patch_sha}" <<'PY'
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
payload = {
    "seed_name": sys.argv[2],
    "patch_path": sys.argv[3],
    "patch_sha256": sys.argv[4],
    "apply_base": "diamond_dpl_evolve",
    "status": "materialized",
    "validation_case": "ariane133_nangate45",
    "validation_final_hpwl_micron": 5502492.2,
    "validation_hpwl_reduction_percent": 4.882028223309753,
}
path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
PY
  git -C "${target_dir}" add .dpl_evolve_seed_manifest.json
  git -C "${target_dir}" commit -m "Record validated sourceTopK seed provenance" >/dev/null
  echo "[INFO] Materialized ${seed_name}: ${target_dir}"
}

ensure_clean_or_reset "${workspace_root}" "${orfs_branch}" "${orfs_base_commit}" "ORFS"

# After pinning the ORFS/OpenROAD anchors, sync nested submodules so OpenROAD
# sources and bundled dependencies (for example src/sta) match the selected
# base commit before we apply any prepare patches.
git -C "${workspace_root}" submodule sync --recursive
if [[ -n "${DPL_EVOLVE_OPENROAD_URL:-}" && "${force}" -eq 1 ]]; then
  cached_openroad="${workspace_root}/.git/modules/tools/OpenROAD"
  cached_url=""
  if [[ -d "${cached_openroad}" ]]; then
    cached_url="$(git -C "${cached_openroad}" config --get remote.origin.url || true)"
  fi
  if [[ -n "${cached_url}" && "${cached_url}" != "${DPL_EVOLVE_OPENROAD_URL}" ]]; then
    echo "[INFO] OpenROAD submodule cache URL changed; clearing cached clone"
    rm -rf "${workspace_root}/tools/OpenROAD" "${cached_openroad}"
    git -C "${workspace_root}" config --remove-section submodule.tools/OpenROAD 2>/dev/null || true
  fi
fi
if [[ -n "${DPL_EVOLVE_OPENROAD_URL:-}" ]]; then
  git -C "${workspace_root}" config submodule.tools/OpenROAD.url "${DPL_EVOLVE_OPENROAD_URL}"
fi
submodule_update_args=(submodule update --init)
if [[ "${force}" -eq 1 ]]; then
  submodule_update_args+=(--force)
fi
git_submodule_update "${workspace_root}" "${submodule_update_args[@]}" tools/OpenROAD
if ! git -C "${workspace_root}/tools/OpenROAD" rev-parse --git-dir >/dev/null 2>&1; then
  echo "[ERROR] Failed to initialize OpenROAD submodule at ${workspace_root}/tools/OpenROAD" >&2
  exit 1
fi
ensure_clean_or_reset "${workspace_root}/tools/OpenROAD" "${openroad_branch}" "${openroad_base_commit}" "OpenROAD"
git -C "${workspace_root}/tools/OpenROAD" submodule sync --recursive
mirror_openroad_nested_submodule_urls "${workspace_root}/tools/OpenROAD"
nested_update_args=(submodule update --init --recursive)
if [[ "${force}" -eq 1 ]]; then
  nested_update_args+=(--force)
fi
git_submodule_update "${workspace_root}/tools/OpenROAD" "${nested_update_args[@]}"
if [[ -f "${workspace_root}/tools/OpenROAD/.gitmodules" ]]; then
  git -C "${workspace_root}/tools/OpenROAD" checkout -- .gitmodules
fi

# Input synthesis is revision-sensitive, so initialize the ORFS-pinned Yosys
# and its nested libraries as part of workspace preparation.
git_submodule_update "${workspace_root}" submodule update --init --recursive tools/yosys

apply_patch_if_needed "${workspace_root}" "${orfs_make_patch}" "ORFS"
apply_patch_if_needed "${workspace_root}/tools/OpenROAD" "${openroad_patch}" "OpenROAD"
snapshot_dpl_seed "diamond_dpl_evolve" "0"
materialize_source_topk_diamond_seed
apply_patch_if_needed "${workspace_root}/tools/OpenROAD" "${openroad_framework_patch}" "OpenROAD"
snapshot_dpl_seed "framework_dpl_evolve" "1"
materialize_default_negotiation_seed

commit_if_dirty "${workspace_root}/tools/OpenROAD" "Prepare constrained dpl_evolve framework"
commit_if_dirty "${workspace_root}" "Prepare ORFS overlay for dpl_evolve_agent" flow/Makefile

echo "[INFO] prepare complete"
echo "[INFO] workspace_root=${workspace_root}"
echo "[INFO] seed_root=${seed_root}"
echo "[INFO] orfs_branch=${orfs_branch:-current}"
echo "[INFO] openroad_branch=${openroad_branch:-current}"
