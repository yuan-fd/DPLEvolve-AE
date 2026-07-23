#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

CHILDREN="${LEVEL1_CHILDREN:-50}"
MAX_PARALLEL="${LEVEL1_MAX_PARALLEL:-10}"
THREADS="${THREADS:-10}"
ACKNOWLEDGE_COST="${ACKNOWLEDGE_LLM_COST:-no}"
STAMP="$(date +%Y%m%d_%H%M%S)"
ROUND_PREFIX="paper_level1_${STAMP}"
FROZEN_OUTPUT="${DPL_EVOLVE_STATE_ROOT}/calibrations/paper_level1/frozen/level1_evidence.md"
FROZEN_MANIFEST="${DPL_EVOLVE_STATE_ROOT}/calibrations/paper_level1/frozen/level1_evidence.json"

usage() {
  cat <<'EOF'
Usage: run_level1.sh [--children N] [--max-parallel N] [--threads N]
                     [--acknowledge-cost] [--dry-run]

Run ReviewDSE Level 1 on the three constructed Nangate45 calibration instances:
JPEG UTIL=90, AES UTIL=70, and SWERV UTIL=60. Each is a one-iteration breadth
mechanism calibration across the framework, Diamond, and Negotiation starts.
After all three reviews complete, their reviewed mechanism records and complete
source-start hashes are frozen into one immutable Level 2 input packet.

The paper specifies the three cases but not the Level 1 Student breadth. This
public reconstruction profile defaults to the framework's documented breadth
calibration value of 50 Students/case. The author-time value must be recovered
for an exact search-process claim.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --children) CHILDREN="$2"; shift 2 ;;
    --max-parallel) MAX_PARALLEL="$2"; shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --acknowledge-cost) ACKNOWLEDGE_COST=yes; shift ;;
    --dry-run) REPRO_DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) repro_die "unknown argument: $1" ;;
  esac
done
repro_positive_integer children "${CHILDREN}"
repro_positive_integer max-parallel "${MAX_PARALLEL}"
repro_positive_integer threads "${THREADS}"
if [[ "${ACKNOWLEDGE_COST}" != yes && "${REPRO_DRY_RUN}" -eq 0 ]]; then
  repro_die "Level 1 launches ${CHILDREN} Students on each of three cases; rerun with ACKNOWLEDGE_LLM_COST=yes"
fi
if [[ "${REPRO_DRY_RUN}" -eq 0 ]]; then repro_require_runtime; fi

CASES=(jpeg_util90_nangate45 aes_nangate45 swerv_wrapper_nangate45)
UTILS=(90 70 60)
FLOW_VARIANTS=(paper_level1_jpeg_util90 paper_level1_aes_util70 paper_level1_swerv_util60)
ROUNDS=()

for index in 0 1 2; do
  case_id="${CASES[$index]}"
  util="${UTILS[$index]}"
  flow_variant="${FLOW_VARIANTS[$index]}"
  round_id="${ROUND_PREFIX}_${case_id}"
  ROUNDS+=("${round_id}")

  repro_run env CORE_UTILIZATION="${util}" \
    "${DPL_EVOLVE_AGENT_ROOT}/scripts/evaluator/run_place_batch.sh" \
    --case "${case_id}" --flow-variant "${flow_variant}" \
    --max-tasks 1 --num-cores "${THREADS}" --target place

  repro_run "${DPL_EVOLVE_PYTHON}" "${DPL_EVOLVE_AGENT_ROOT}/scripts/optimize_case_with_codex.py" \
    --case "${case_id}" \
    --flow-variant "${flow_variant}" \
    --round-id "${round_id}" \
    --start-kind framework \
    --iterations 1 \
    --children "${CHILDREN}" \
    --max-parallel "${MAX_PARALLEL}" \
    --threads "${THREADS}" \
    --student-runtime-multiplier 2.0 \
    --teacher-model gpt-5.5 \
    --teacher-reasoning-effort xhigh \
    --student-model gpt-5.4 \
    --student-reasoning-effort xhigh \
    --calibration-mode \
    --calibrate-start-seeds \
    --skip-core-build \
    --audit-prompts \
    --launch
done

freeze_args=(
  "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/freeze_level1.py"
  --state-root "${DPL_EVOLVE_STATE_ROOT}"
  --children-per-case "${CHILDREN}"
  --output "${FROZEN_OUTPUT}"
  --manifest-output "${FROZEN_MANIFEST}"
)
for round_id in "${ROUNDS[@]}"; do freeze_args+=(--round "${round_id}"); done
repro_run "${freeze_args[@]}"
repro_note "frozen Level 1 packet: ${FROZEN_OUTPUT}"
repro_run "${DPL_EVOLVE_PYTHON}" "${SCRIPT_DIR}/verify_level1.py" \
  --packet "${FROZEN_OUTPUT}" --manifest "${FROZEN_MANIFEST}"
