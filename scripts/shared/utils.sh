#!/usr/bin/env bash
# DPLEvolve AE — Shared Utility Functions
# Source this in all AE scripts for common helpers.

set -euo pipefail

# ── Logging ─────────────────────────────────────────────────────────────────
dpl_ae_info()  { echo "[INFO]  $*"; }
dpl_ae_warn()  { echo "[WARN]  $*" >&2; }
dpl_ae_error() { echo "[ERROR] $*" >&2; }
dpl_ae_ok()    { echo "[OK]    $*"; }

# ── Timestamp ───────────────────────────────────────────────────────────────
dpl_ae_timestamp() {
  date +%Y%m%d_%H%M%S
}

# ── SHA-256 ─────────────────────────────────────────────────────────────────
dpl_ae_sha256() {
  local path="$1"
  if [[ ! -f "${path}" ]]; then
    echo "FILE_NOT_FOUND"
    return 1
  fi
  sha256sum "${path}" | awk '{print $1}'
}

# ── Read JSON value using Python ────────────────────────────────────────────
dpl_ae_json_get() {
  local json_file="$1"
  local key_path="$2"
  "${DPL_EVOLVE_PYTHON}" -c "
import json, sys
data = json.load(open(sys.argv[1], encoding='utf-8'))
keys = sys.argv[2].split('.')
for k in keys:
    if isinstance(data, dict):
        data = data.get(k)
    elif isinstance(data, list):
        try:
            idx = int(k)
            data = data[idx]
        except (ValueError, IndexError):
            print('')
            sys.exit(1)
    else:
        print('')
        sys.exit(1)
if data is None:
    sys.exit(1)
print(data)
" "${json_file}" "${key_path}" 2>/dev/null
}

# ── Check command exists ────────────────────────────────────────────────────
dpl_ae_require_cmd() {
  local cmd="$1"
  if ! command -v "${cmd}" >/dev/null 2>&1; then
    dpl_ae_error "Missing required command: ${cmd}"
    return 1
  fi
  return 0
}

# ── Check directory is a git repo at expected commit ────────────────────────
dpl_ae_check_git_commit() {
  local repo_path="$1"
  local expected_commit="$2"
  local label="$3"

  if [[ ! -d "${repo_path}/.git" ]]; then
    dpl_ae_error "${label}: not a git repository at ${repo_path}"
    return 1
  fi

  local actual
  actual="$(git -C "${repo_path}" rev-parse HEAD)"
  if [[ "${actual}" != "${expected_commit}" ]]; then
    dpl_ae_error "${label}: commit mismatch"
    dpl_ae_error "  expected: ${expected_commit}"
    dpl_ae_error "  actual:   ${actual}"
    return 1
  fi
  dpl_ae_ok "${label}: commit verified (${expected_commit:0:8})"
  return 0
}

# ── Float comparison with tolerance ─────────────────────────────────────────
dpl_ae_float_within_tolerance() {
  local actual="$1"
  local expected="$2"
  local tolerance="$3"
  local label="$4"

  "${DPL_EVOLVE_PYTHON}" -c "
import sys
actual = float(sys.argv[1])
expected = float(sys.argv[2])
tolerance = float(sys.argv[3])
label = sys.argv[4]
delta = abs(actual - expected)
if delta > tolerance:
    print(f'  FAIL [{label}]: actual={actual}, expected={expected}, delta={delta}, tolerance={tolerance}')
    sys.exit(1)
else:
    print(f'  PASS [{label}]: actual={actual}, expected={expected}, delta={delta:.6f}')
    sys.exit(0)
" "${actual}" "${expected}" "${tolerance}" "${label}"
}

# ── Ensure new run doesn't overwrite existing ───────────────────────────────
dpl_ae_require_new_run() {
  local target_dir="$1"
  if [[ -e "${target_dir}" ]]; then
    dpl_ae_error "Refusing to overwrite existing run: ${target_dir}"
    dpl_ae_error "Use a new flow variant or run tag."
    return 1
  fi
  return 0
}
