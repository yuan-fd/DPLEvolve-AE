#!/usr/bin/env bash
# DPLEvolve AE — Web Demo Start Script
# Usage: bash start.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

bash "${SCRIPT_DIR}/setup.sh"
source .venv/bin/activate
echo "Python: $(python3 --version)"

# Defaults are portable and private. Use an SSH tunnel instead of exposing the
# reviewer console directly to a network.
export DPLEVOLVE_AE_ROOT="${DPLEVOLVE_AE_ROOT:-$(cd .. && pwd)}"
export DPLEVOLVE_WEB_HOST="${DPLEVOLVE_WEB_HOST:-127.0.0.1}"
export DPLEVOLVE_WEB_PORT="${DPLEVOLVE_WEB_PORT:-8080}"

echo ""
echo "══════════════════════════════════════════════════"
echo "  DPLEvolve Artifact Reviewer Console"
echo "  Address: ${DPLEVOLVE_WEB_HOST}:${DPLEVOLVE_WEB_PORT}"
echo "  AE Root: ${DPLEVOLVE_AE_ROOT}"
echo "══════════════════════════════════════════════════"
echo ""
echo "Starting server..."
echo "Open http://${DPLEVOLVE_WEB_HOST}:${DPLEVOLVE_WEB_PORT} in your browser"
echo ""

python3 server.py
