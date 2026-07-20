#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AGENT_ROOT="${DPL_EVOLVE_AGENT_ROOT:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
BASELINE_DIR="${AGENT_ROOT}/baseline"

usage() {
  cat <<'EOF'
Usage: run_canonical_line.sh --line NAME [options]

Canonical lines:
  openroad_dpl_flow
  openroad_dpl_negotiation
  evolve_default

Options:
  --line NAME              Canonical line to run.
  --case ID                Case id under problems/ (passed through).
  --openroad-binary PATH   Optional explicit OpenROAD binary.
  --help                   Show this message.

All remaining arguments are passed through to the selected baseline wrapper.
EOF
}

line=""
openroad_binary=""
pass_through=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --line)
      line="$2"
      shift 2
      ;;
    --openroad-binary)
      openroad_binary="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      pass_through+=("$1")
      shift
      ;;
  esac
done

if [[ -z "${line}" ]]; then
  echo "Missing required --line." >&2
  usage >&2
  exit 1
fi

if [[ -n "${openroad_binary}" ]]; then
  pass_through+=(--openroad-binary "$(realpath -sm "${openroad_binary}")")
fi

case "${line}" in
  openroad_dpl_flow)
    exec "${BASELINE_DIR}/run_openroad_dpl_flow.sh" "${pass_through[@]}"
    ;;
  openroad_dpl_negotiation)
    exec "${BASELINE_DIR}/run_openroad_dpl_negotiation.sh" "${pass_through[@]}"
    ;;
  evolve_default)
    exec "${BASELINE_DIR}/run_evolve_default.sh" "${pass_through[@]}"
    ;;
  *)
    echo "Unsupported canonical line: ${line}" >&2
    usage >&2
    exit 1
    ;;
esac
