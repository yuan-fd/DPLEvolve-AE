#!/usr/bin/env bash
set -euo pipefail

_dpl_is_orfs_root() {
  local candidate="$1"
  [[ -d "${candidate}/flow" && -d "${candidate}/tools/OpenROAD" ]]
}

_dpl_default_orfs_root() {
  local agent_root="$1"
  realpath -m "${agent_root}/../OpenROAD-flow-scripts"
}

_dpl_contains_space() {
  [[ "$1" == *" "* ]]
}

_dpl_path_id() {
  if command -v sha1sum >/dev/null 2>&1; then
    printf '%s' "$1" | sha1sum | awk '{print $1}'
  else
    printf '%s' "$1" | cksum | awk '{print $1}'
  fi
}

_dpl_python_candidates() {
  local seen=":" candidate raw

  if [[ -n "${DPL_EVOLVE_PYTHON:-}" ]]; then
    printf '%s\n' "${DPL_EVOLVE_PYTHON}"
    seen="${seen}${DPL_EVOLVE_PYTHON}:"
  fi

  if [[ -n "${DPL_EVOLVE_PYTHON_CANDIDATES:-}" ]]; then
    while IFS= read -r -d '' raw; do
      candidate="${raw}"
      [[ -z "${candidate}" ]] && continue
      if [[ "${seen}" != *":${candidate}:"* ]]; then
        printf '%s\n' "${candidate}"
        seen="${seen}${candidate}:"
      fi
    done < <(printf '%s' "${DPL_EVOLVE_PYTHON_CANDIDATES}" | tr ':' '\0')
  fi

  for candidate in python3 python; do
    if [[ "${seen}" != *":${candidate}:"* ]]; then
      printf '%s\n' "${candidate}"
      seen="${seen}${candidate}:"
    fi
  done
}

dpl_resolve_python() {
  local required_module="${1:-yaml}"
  local candidate resolved

  while IFS= read -r candidate; do
    [[ -z "${candidate}" ]] && continue
    if [[ "${candidate}" = */* ]]; then
      [[ -x "${candidate}" ]] || continue
      resolved="${candidate}"
    else
      resolved="$(command -v "${candidate}" 2>/dev/null || true)"
      [[ -n "${resolved}" ]] || continue
    fi

    if [[ -n "${required_module}" ]]; then
      if ! "${resolved}" -c "import ${required_module}" >/dev/null 2>&1; then
        continue
      fi
    fi
    export DPL_EVOLVE_PYTHON="${resolved}"
    return 0
  done < <(_dpl_python_candidates)

  if [[ -n "${required_module}" ]]; then
    echo "[ERROR] Could not find a Python interpreter that can import ${required_module}." >&2
  else
    echo "[ERROR] Could not find a usable Python interpreter." >&2
  fi
  echo "[ERROR] Set DPL_EVOLVE_PYTHON or DPL_EVOLVE_PYTHON_CANDIDATES in env.sh." >&2
  return 1
}

_dpl_lexical_alias_path() {
  local raw="$1"
  local parent base
  parent="$(dirname "${raw}")"
  base="$(basename "${raw}")"
  parent="$(realpath -m "${parent}")"
  printf '%s/%s\n' "${parent%/}" "${base}"
}

_dpl_symlink_target() {
  local link_path="$1"
  local target
  target="$(readlink "${link_path}")"
  if [[ "${target}" = /* ]]; then
    realpath -m "${target}"
  else
    realpath -m "$(dirname "${link_path}")/${target}"
  fi
}

_dpl_stable_alias_root() {
  local alias_path parent

  if [[ -n "${DPL_EVOLVE_ORFS_ALIAS_ROOT:-}" ]]; then
    alias_path="$(_dpl_lexical_alias_path "${DPL_EVOLVE_ORFS_ALIAS_ROOT}")"
  elif ! _dpl_contains_space "${DPL_EVOLVE_STATE_ROOT}"; then
    alias_path="$(_dpl_lexical_alias_path "${DPL_EVOLVE_STATE_ROOT}/orfs_mount")"
  else
    echo "[ERROR] ORFS_ROOT contains spaces and DPL_EVOLVE_STATE_ROOT also contains spaces." >&2
    echo "[ERROR] Set DPL_EVOLVE_ORFS_ALIAS_ROOT to a space-free local path in env.sh." >&2
    return 1
  fi

  parent="$(dirname "${alias_path}")"
  mkdir -p "${parent}"

  if [[ -L "${alias_path}" ]]; then
    if [[ "$(_dpl_symlink_target "${alias_path}")" != "${ORFS_ROOT}" ]]; then
      rm -f "${alias_path}"
      ln -s "${ORFS_ROOT}" "${alias_path}"
    fi
  elif [[ -e "${alias_path}" ]]; then
    if ! _dpl_is_orfs_root "${alias_path}"; then
      echo "[ERROR] Stable ORFS alias path exists but is not an ORFS workspace: ${alias_path}" >&2
      return 1
    fi
  else
    ln -s "${ORFS_ROOT}" "${alias_path}"
  fi

  printf '%s\n' "${alias_path}"
}

dpl_resolve_agent_root() {
  if [[ -n "${DPL_EVOLVE_AGENT_ROOT:-}" ]]; then
    realpath -m "${DPL_EVOLVE_AGENT_ROOT}"
    return 0
  fi
  local helper_dir
  helper_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  realpath -m "${helper_dir}/.."
}

dpl_source_local_env_preserving_overrides() {
  local agent_root="$1"
  local caller_agent_root_set="${DPL_EVOLVE_AGENT_ROOT+x}"
  local caller_orfs_root_set="${ORFS_ROOT+x}"
  local caller_orfs_build_root_set="${ORFS_BUILD_ROOT+x}"
  local caller_state_root_set="${DPL_EVOLVE_STATE_ROOT+x}"
  local caller_orfs_alias_root_set="${DPL_EVOLVE_ORFS_ALIAS_ROOT+x}"
  local caller_python_set="${DPL_EVOLVE_PYTHON+x}"
  local caller_python_candidates_set="${DPL_EVOLVE_PYTHON_CANDIDATES+x}"
  local caller_agent_root="${DPL_EVOLVE_AGENT_ROOT:-}"
  local caller_orfs_root="${ORFS_ROOT:-}"
  local caller_orfs_build_root="${ORFS_BUILD_ROOT:-}"
  local caller_state_root="${DPL_EVOLVE_STATE_ROOT:-}"
  local caller_orfs_alias_root="${DPL_EVOLVE_ORFS_ALIAS_ROOT:-}"
  local caller_python="${DPL_EVOLVE_PYTHON:-}"
  local caller_python_candidates="${DPL_EVOLVE_PYTHON_CANDIDATES:-}"

  if [[ "${DPL_EVOLVE_SKIP_ENV_SH:-0}" == "1" || ! -f "${agent_root}/env.sh" ]]; then
    return 0
  fi

  # env.sh is local-only and ignored by git. It provides machine-specific
  # defaults; caller-provided roots/tool overrides win.
  # shellcheck source=/dev/null
  source "${agent_root}/env.sh"
  [[ -n "${caller_agent_root_set}" ]] && export DPL_EVOLVE_AGENT_ROOT="${caller_agent_root}"
  [[ -n "${caller_orfs_root_set}" ]] && export ORFS_ROOT="${caller_orfs_root}"
  [[ -n "${caller_orfs_build_root_set}" ]] && export ORFS_BUILD_ROOT="${caller_orfs_build_root}"
  [[ -n "${caller_state_root_set}" ]] && export DPL_EVOLVE_STATE_ROOT="${caller_state_root}"
  [[ -n "${caller_orfs_alias_root_set}" ]] && export DPL_EVOLVE_ORFS_ALIAS_ROOT="${caller_orfs_alias_root}"
  [[ -n "${caller_python_set}" ]] && export DPL_EVOLVE_PYTHON="${caller_python}"
  [[ -n "${caller_python_candidates_set}" ]] && export DPL_EVOLVE_PYTHON_CANDIDATES="${caller_python_candidates}"
  return 0
}

dpl_init_runtime() {
  local script_name="${1:-script}"
  local agent_root candidate

  agent_root="$(dpl_resolve_agent_root)"
  export DPL_EVOLVE_AGENT_ROOT="${agent_root}"
  if [[ "${DPL_EVOLVE_SKIP_ENV_SH:-0}" != "1" && -f "${agent_root}/env.sh" ]]; then
    dpl_source_local_env_preserving_overrides "${agent_root}"
    agent_root="$(dpl_resolve_agent_root)"
    export DPL_EVOLVE_AGENT_ROOT="${agent_root}"
  fi

  if [[ -n "${ORFS_ROOT:-}" ]]; then
    candidate="$(realpath -m "${ORFS_ROOT}")"
    if ! _dpl_is_orfs_root "${candidate}"; then
      echo "[ERROR] ${script_name} expected ORFS_ROOT to point at an ORFS workspace, got: ${candidate}" >&2
      return 1
    fi
    export ORFS_ROOT="${candidate}"
  elif _dpl_is_orfs_root "$(_dpl_default_orfs_root "${agent_root}")"; then
    export ORFS_ROOT="$(_dpl_default_orfs_root "${agent_root}")"
  else
    echo "[ERROR] ${script_name} could not locate an ORFS workspace." >&2
    echo "[ERROR] Set ORFS_ROOT to a workspace containing flow/ and tools/OpenROAD/." >&2
    echo "[ERROR] Default sibling lookup also failed: $(_dpl_default_orfs_root "${agent_root}")" >&2
    return 1
  fi

  if [[ -n "${DPL_EVOLVE_STATE_ROOT:-}" ]]; then
    export DPL_EVOLVE_STATE_ROOT="$(realpath -m "${DPL_EVOLVE_STATE_ROOT}")"
  else
    export DPL_EVOLVE_STATE_ROOT="$(realpath -m "${agent_root}/../dpl_evolve_state")"
  fi

  export DPL_EVOLVE_PACKET_DIR="${DPL_EVOLVE_STATE_ROOT}/packets"
  export DPL_EVOLVE_CHECKPOINTS_DIR="${DPL_EVOLVE_STATE_ROOT}/checkpoints"
  export DPL_EVOLVE_OPERATIONS_DIR="${DPL_EVOLVE_CHECKPOINTS_DIR}/operations"
  dpl_resolve_python yaml

  if _dpl_contains_space "${ORFS_ROOT}"; then
    export ORFS_BUILD_ROOT="$(_dpl_stable_alias_root)"
  else
    export ORFS_BUILD_ROOT="${ORFS_ROOT}"
  fi
}
