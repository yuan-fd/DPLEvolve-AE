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
TEACHER_MODEL="${TEACHER_MODEL:-gpt-5.5}"
TEACHER_REASONING_EFFORT="${TEACHER_REASONING_EFFORT:-xhigh}"
STUDENT_MODEL="${STUDENT_MODEL:-gpt-5.4}"
STUDENT_REASONING_EFFORT="${STUDENT_REASONING_EFFORT:-xhigh}"

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
  --teacher-model NAME  Teacher model. Default: gpt-5.5.
  --teacher-reasoning-effort E
                        Teacher effort: low, medium, high, or xhigh.
  --student-model NAME  Student model. Default: gpt-5.4.
  --student-reasoning-effort E
                        Student effort: low, medium, high, or xhigh.
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
    --teacher-model) TEACHER_MODEL="$2"; shift 2 ;;
    --teacher-reasoning-effort) TEACHER_REASONING_EFFORT="$2"; shift 2 ;;
    --student-model) STUDENT_MODEL="$2"; shift 2 ;;
    --student-reasoning-effort) STUDENT_REASONING_EFFORT="$2"; shift 2 ;;
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
  --teacher-model "${TEACHER_MODEL}"
  --teacher-reasoning-effort "${TEACHER_REASONING_EFFORT}"
  --student-model "${STUDENT_MODEL}"
  --student-reasoning-effort "${STUDENT_REASONING_EFFORT}"
  --skip-core-build
)
if [[ -n "${RUN_PREFIX}" ]]; then
  args+=(--run-prefix "${RUN_PREFIX}")
fi

if [[ "${PROFILE}" == paper ]]; then
  if [[ -z "${RUN_PREFIX}" && "${REPRO_DRY_RUN}" -eq 0 ]]; then
    RUN_PREFIX="paper_dse_$(date +%Y%m%d_%H%M%S)"
    args+=(--run-prefix "${RUN_PREFIX}")
  fi
  if [[ "${ACKNOWLEDGE_COST}" != yes && "${REPRO_DRY_RUN}" -eq 0 ]]; then
    repro_die "paper DSE is about 2.15B logged tokens per target; rerun with ACKNOWLEDGE_LLM_COST=yes"
  fi
  if [[ "${REPRO_DRY_RUN}" -eq 0 && ! -f "${LEVEL1_EVIDENCE}" ]]; then
    repro_die "frozen Level 1 evidence is missing: ${LEVEL1_EVIDENCE}; run 'make reproduce-level1' first"
  fi
  if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then
    repro_run "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/verify_level1.py" \
      --packet "${LEVEL1_EVIDENCE}"
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

if [[ "${PROFILE}" == paper && "${REPRO_DRY_RUN}" -eq 0 ]]; then
  batch_root="${DPL_EVOLVE_STATE_ROOT}/experiment_batches/${RUN_PREFIX}_${FLOW_VARIANT}"
  repro_note "selecting HPWL/G_HR winners from the complete protected candidate population"
  repro_run "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/summarize_dse_campaign.py" \
    --batch-root "${batch_root}" \
    --orfs-root "${ORFS_ROOT}" \
    --expected "${AE_ROOT}/artifacts/01-table4-qor/expected/table4.json" \
    --output "${batch_root}/table4-search.tsv" \
    --audit-output "${batch_root}/candidate-eligibility-audit.json"
  repro_note "full-search summary: ${batch_root}/table4-search.tsv"
fi
