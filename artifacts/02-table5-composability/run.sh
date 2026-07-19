#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_BIN="${DPL_EVOLVE_PYTHON:-python3}"
exec "${PYTHON_BIN}" "${ROOT}/verify.py"
