#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

FLOW_VARIANT="${FLOW_VARIANT:-paper9_place}"
TRIALS="${TRIALS:-400}"
THREADS="${THREADS:-10}"
MAX_CONCURRENT_CASES="${MAX_CONCURRENT_CASES:-3}"
MAX_CONCURRENT_TRIALS="${MAX_CONCURRENT_TRIALS:-4}"
CASES=()

usage() {
  cat <<'EOF'
Usage: run_bo.sh [options]

Run the paper's public-knob BO baseline with Optuna TPE.

Options:
  --case ID                 Run one case; may be repeated. Default: paper nine.
  --flow-variant NAME       Prepared input variant. Default: paper9_place.
  --trials N                Trials per case. Paper value: 400.
  --threads N               Threads per trial. Default: 10.
  --max-concurrent-cases N  Concurrent cases. Default: 3.
  --max-concurrent-trials N Concurrent TPE trials per case. Paper value: 4.
  --dry-run                 Print all case commands without running Ray/OpenROAD.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --case) CASES+=("$2"); shift 2 ;;
    --flow-variant) FLOW_VARIANT="$2"; shift 2 ;;
    --trials) TRIALS="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --max-concurrent-cases) MAX_CONCURRENT_CASES="$2"; shift 2 ;;
    --max-concurrent-trials) MAX_CONCURRENT_TRIALS="$2"; shift 2 ;;
    --dry-run) REPRO_DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) repro_die "unknown argument: $1" ;;
  esac
done

for pair in "trials:${TRIALS}" "threads:${THREADS}" "max-concurrent-cases:${MAX_CONCURRENT_CASES}" "max-concurrent-trials:${MAX_CONCURRENT_TRIALS}"; do
  repro_positive_integer "${pair%%:*}" "${pair#*:}"
done

args=()
for case_id in "${CASES[@]}"; do args+=(--case "${case_id}"); done
if [[ "${REPRO_DRY_RUN}" -eq 1 ]]; then args+=(--dry-run); else repro_require_runtime; fi

export BO_FLOW_VARIANT="${FLOW_VARIANT}"
export BO_TRIALS="${TRIALS}"
export BO_THREADS_PER_TRIAL="${THREADS}"
export BO_MAX_CONCURRENT_CASES="${MAX_CONCURRENT_CASES}"
export BO_MAX_CONCURRENT_TRIALS="${MAX_CONCURRENT_TRIALS}"
export BO_RAY_CPUS="${MAX_CONCURRENT_TRIALS}"
repro_run "${DPL_EVOLVE_AGENT_ROOT}/experiments/launchers/run_bo_9case_openroad_dpl.sh" "${args[@]}"
