#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

PROFILE="small"
FLOW_VARIANT="${FLOW_VARIANT:-paper9_place}"
THREADS="${THREADS:-10}"
CASE_ID="aes_nangate45"
CHILDREN="1"
ITERATIONS="1"
ACKNOWLEDGE_COST="${ACKNOWLEDGE_LLM_COST:-no}"
LEVEL1_EVIDENCE="${LEVEL1_EVIDENCE:-${DPL_EVOLVE_STATE_ROOT}/calibrations/paper_level1/frozen/level1_evidence.md}"
RUN_PREFIX="${DSE_RUN_PREFIX:-}"

usage() {
  cat <<'EOF'
Usage: run_dse.sh --profile small|paper [options]

Profiles:
  small  One target, one Student, one iteration by default. This is a real
         source-edit/build/evaluate/review loop for exercising the method.
  paper  All nine targets, 1 GPT-5.5 xhigh Teacher, 4 GPT-5.4 xhigh Students,
         10 iterations, and the 2x runtime gate. Requires the explicit
         acknowledgement ACKNOWLEDGE_LLM_COST=yes.

Options:
  --case ID             Small-profile target. Default: aes_nangate45.
  --children N          Small-profile Student count. Default: 1.
  --iterations N        Small-profile review iterations. Default: 1.
  --flow-variant NAME   Prepared input variant. Default: paper9_place.
  --threads N           OpenROAD threads per evaluation. Default: 10.
  --acknowledge-cost    Equivalent to ACKNOWLEDGE_LLM_COST=yes.
  --level1-evidence P   Frozen output of make reproduce-level1.
  --run-prefix NAME     Stable prefix used to locate this run when rebuilding
                        Figures 4/5. Can also be set with DSE_RUN_PREFIX.
  --dry-run             Print the exact launch configuration; no API/EDA work.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile) PROFILE="$2"; shift 2 ;;
    --case) CASE_ID="$2"; shift 2 ;;
    --children) CHILDREN="$2"; shift 2 ;;
    --iterations) ITERATIONS="$2"; shift 2 ;;
    --flow-variant) FLOW_VARIANT="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --acknowledge-cost) ACKNOWLEDGE_COST=yes; shift ;;
    --level1-evidence) LEVEL1_EVIDENCE="$2"; shift 2 ;;
    --run-prefix) RUN_PREFIX="$2"; shift 2 ;;
    --dry-run) REPRO_DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) repro_die "unknown argument: $1" ;;
  esac
done

[[ "${PROFILE}" == small || "${PROFILE}" == paper ]] || repro_die "--profile must be small or paper"
repro_positive_integer threads "${THREADS}"

args=(
  --flow-variant "${FLOW_VARIANT}"
  --threads "${THREADS}"
  --start-kind framework
  --runtime-multiplier 2.0
  --teacher-model gpt-5.5
  --teacher-reasoning-effort xhigh
  --student-model gpt-5.4
  --student-reasoning-effort xhigh
  --skip-core-build
)
if [[ -n "${RUN_PREFIX}" ]]; then
  args+=(--run-prefix "${RUN_PREFIX}")
fi

if [[ "${PROFILE}" == paper ]]; then
  if [[ "${ACKNOWLEDGE_COST}" != yes && "${REPRO_DRY_RUN}" -eq 0 ]]; then
    repro_die "paper DSE is about 2.15B logged tokens per target; rerun with ACKNOWLEDGE_LLM_COST=yes"
  fi
  if [[ "${REPRO_DRY_RUN}" -eq 0 && ! -f "${LEVEL1_EVIDENCE}" ]]; then
    repro_die "frozen Level 1 evidence is missing: ${LEVEL1_EVIDENCE}; run 'make reproduce-level1' first"
  fi
  args+=(--level1-evidence "${LEVEL1_EVIDENCE}")
  args+=(--case-set evolve_9case --children 4 --iterations 10 --max-parallel 4 --max-concurrent-cases 3)
else
  repro_positive_integer children "${CHILDREN}"
  repro_positive_integer iterations "${ITERATIONS}"
  args+=(--case "${CASE_ID}" --children "${CHILDREN}" --iterations "${ITERATIONS}" --max-parallel "${CHILDREN}" --max-concurrent-cases 1)
  if [[ -f "${LEVEL1_EVIDENCE}" ]]; then args+=(--level1-evidence "${LEVEL1_EVIDENCE}"); fi
fi

if [[ "${REPRO_DRY_RUN}" -eq 1 ]]; then
  args+=(--dry-run)
else
  repro_require_runtime
fi

repro_note "launching ${PROFILE} ReviewDSE profile"
repro_run "${DPL_EVOLVE_AGENT_ROOT}/experiments/launchers/run_evolve_9case_place_batch.sh" "${args[@]}"
