# DPLEvolve Artifact Evaluation — Environment Module Helper
# Source this file to load required server modules.
# This is a template; adjust for your site's module system.

_dplevolve_load_modules() {
  if ! type module >/dev/null 2>&1; then
    echo "[dplevolve] Environment Modules not found on PATH; using system defaults." >&2
    return 0
  fi

  # GCC — required for C++17 OpenROAD build
  if ! module is-loaded gcc/default 2>/dev/null; then
    module load gcc/default 2>/dev/null || {
      echo "[dplevolve] WARNING: Could not load gcc/default module." >&2
      echo "[dplevolve] Ensure GCC >= 9 is on PATH." >&2
    }
  fi

  # OpenROAD system module — provides Bison, Flex, and shared libraries
  if ! module is-loaded openroad 2>/dev/null; then
    module load openroad 2>/dev/null || {
      echo "[dplevolve] WARNING: Could not load openroad module." >&2
      echo "[dplevolve] Bison/Flex build dependencies may be missing." >&2
    }
  fi

  return 0
}

# Only auto-load when sourced directly, not when pre-checking
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  _dplevolve_load_modules
fi
