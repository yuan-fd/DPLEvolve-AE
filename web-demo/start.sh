#!/usr/bin/env bash
# DPLEvolve AE — Web Demo Start Script
# Usage: bash start.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Check Python
if ! command -v python3 &>/dev/null; then
    echo "ERROR: python3 not found. Run: bash ../scripts/human/doctor.sh"
    exit 1
fi

if ! python3 -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 11) else 1)' 2>/dev/null; then
    echo "ERROR: Python 3.11 or newer is required. Run: bash ../scripts/human/doctor.sh"
    exit 1
fi

if ! python3 -c 'import venv, ensurepip' 2>/dev/null; then
    echo "ERROR: Python venv/ensurepip support is missing."
    echo "Run 'bash ../scripts/human/doctor.sh' for the exact package command."
    exit 1
fi

echo "Python: $(python3 --version)"

# Create venv if not present
if [ ! -d ".venv" ]; then
    echo "Creating virtual environment..."
    python3 -m venv .venv
fi

# Activate and install deps
source .venv/bin/activate
echo "Installing dependencies..."
if ! pip install -q -r requirements.txt; then
    echo "ERROR: Web dependencies could not be installed. Check network/proxy settings."
    exit 1
fi

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
