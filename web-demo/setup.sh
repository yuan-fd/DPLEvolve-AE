#!/usr/bin/env bash
# Prepare the isolated Python environment used by the reviewer console and tests.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="${SCRIPT_DIR}/.venv"
REQUIREMENTS="${SCRIPT_DIR}/requirements.txt"
STAMP="${VENV_DIR}/.requirements.sha256"

if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 not found. Run: bash ../scripts/human/doctor.sh" >&2
    exit 1
fi

if ! python3 -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 11) else 1)' 2>/dev/null; then
    echo "ERROR: Python 3.11 or newer is required." >&2
    exit 1
fi

if ! python3 -c 'import venv, ensurepip' 2>/dev/null; then
    echo "ERROR: Python venv/ensurepip support is missing." >&2
    echo "Run 'bash ../scripts/human/doctor.sh' for the exact package command." >&2
    exit 1
fi

requirements_sha="$({ python3 - "${REQUIREMENTS}" <<'PY'
import hashlib
import pathlib
import sys

print(hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest())
PY
} )"

if [[ ! -x "${VENV_DIR}/bin/python" ]]; then
    echo "Creating Web Demo virtual environment..."
    python3 -m venv "${VENV_DIR}"
fi

installed_sha=""
if [[ -f "${STAMP}" ]]; then
    installed_sha="$(<"${STAMP}")"
fi

if [[ "${installed_sha}" != "${requirements_sha}" ]] \
   || ! "${VENV_DIR}/bin/python" -c 'import fastapi, paramiko, uvicorn, websockets' 2>/dev/null; then
    echo "Installing Web Demo dependencies..."
    "${VENV_DIR}/bin/python" -m pip install -q -r "${REQUIREMENTS}"
    printf '%s\n' "${requirements_sha}" > "${STAMP}"
fi

echo "Web Demo environment ready: ${VENV_DIR}/bin/python"
