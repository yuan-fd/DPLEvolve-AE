#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

FLOW_VARIANT="${FLOW_VARIANT:-paper9_place}"
THREADS="${THREADS:-10}"
MAX_PARALLEL="${MAX_PARALLEL:-3}"
CASES=()

usage() {
  cat <<'EOF'
Usage: run_baselines.sh [--case ID ...] [--flow-variant NAME] [--threads N]
                        [--max-parallel N] [--dry-run]

Run the fresh OpenROAD default-DPL reference used to normalize Table 4.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --case) CASES+=("$2"); shift 2 ;;
    --flow-variant) FLOW_VARIANT="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --max-parallel) MAX_PARALLEL="$2"; shift 2 ;;
    --dry-run) REPRO_DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) repro_die "unknown argument: $1" ;;
  esac
done

repro_positive_integer threads "${THREADS}"
repro_positive_integer max-parallel "${MAX_PARALLEL}"
args=(--flow-variant "${FLOW_VARIANT}" --threads "${THREADS}" --max-parallel "${MAX_PARALLEL}")
for case_id in "${CASES[@]}"; do args+=(--case "${case_id}"); done
if [[ "${REPRO_DRY_RUN}" -eq 1 ]]; then args+=(--dry-run); else repro_require_runtime; fi
repro_run "${DPL_EVOLVE_AGENT_ROOT}/experiments/launchers/run_openroad_dpl_9case_baselines.sh" "${args[@]}"
