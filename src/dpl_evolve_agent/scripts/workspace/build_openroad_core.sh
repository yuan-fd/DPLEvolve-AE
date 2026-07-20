#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOTSTRAP_AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
BOOTSTRAP_AGENT_ROOT="$(realpath -m "${BOOTSTRAP_AGENT_ROOT}")"
source "${BOOTSTRAP_AGENT_ROOT}/scripts/runtime_env.sh"
dpl_init_runtime "build_openroad_core.sh"

THREADS="8"
OPENROAD_SRC="${ORFS_ROOT}/tools/OpenROAD"
OPENROAD_ANCHOR_ID="$(git -C "${OPENROAD_SRC}" rev-parse --short HEAD)"
CORE_ROOT="${DPL_EVOLVE_STATE_ROOT}/openroad_core/${OPENROAD_ANCHOR_ID}"
BUILD_DIR=""
INSTALL_ROOT=""

usage() {
  cat <<EOF
Usage: build_openroad_core.sh [options] [threads]

Options:
  --core-root PATH         Core seed root. Default:
                           ${DPL_EVOLVE_STATE_ROOT}/openroad_core/${OPENROAD_ANCHOR_ID}
  --build-dir PATH         Explicit build directory override.
  --install-root PATH      Explicit install root override.
  --threads N              Build threads. Default: 8.
  --help                   Show this message.

Notes:
  OPENROAD_* environment variables are not used as input defaults.  This
  wrapper exports explicit paths only for the internal build helper it calls.
  The install root is the stable seed binary location for private variant
  relinks.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --core-root)
      CORE_ROOT="$2"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --install-root)
      INSTALL_ROOT="$2"
      shift 2
      ;;
    --threads)
      THREADS="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      if [[ "$1" =~ ^[0-9]+$ ]]; then
        THREADS="$1"
        shift
      else
        echo "[ERROR] Unknown argument: $1" >&2
        usage >&2
        exit 1
      fi
      ;;
  esac
done

CORE_ROOT="$(realpath -m "${CORE_ROOT}")"
BUILD_DIR="${BUILD_DIR:-${CORE_ROOT}/build}"
INSTALL_ROOT="${INSTALL_ROOT:-${CORE_ROOT}/install/OpenROAD}"
BUILD_DIR="$(realpath -m "${BUILD_DIR}")"
INSTALL_ROOT="$(realpath -m "${INSTALL_ROOT}")"

"${DPL_EVOLVE_AGENT_ROOT}/scripts/workspace/build_openroad_only.sh" \
  --threads "${THREADS}" \
  --core-root "${CORE_ROOT}" \
  --build-dir "${BUILD_DIR}" \
  --install-root "${INSTALL_ROOT}" \
  --openroad-binary "${INSTALL_ROOT}/bin/openroad"
